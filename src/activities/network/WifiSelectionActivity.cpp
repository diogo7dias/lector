#include "WifiSelectionActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_mac.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "WifiCredentialStore.h"
#include "activities/util/KeyboardEntryActivity.h"

namespace {
// The reader used to fail a connection with nothing on record but a timeout.
// The SDK knows why it failed -- wrong password, AP out of range, the AP
// dropping us -- and names the reason, so log it. Diagnosis only; the reason
// never steers a decision here.
void logWifiStationEvent(const arduino_event_id_t event, const arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      LOG_INF("WIFI", "Station connected, channel %d", static_cast<int>(info.wifi_sta_connected.channel));
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      LOG_INF("WIFI", "Station got IP, rssi %d dBm", static_cast<int>(WiFi.RSSI()));
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
      const auto reason = static_cast<wifi_err_reason_t>(info.wifi_sta_disconnected.reason);
      LOG_INF("WIFI", "Station disconnected: %s (%d)", WiFi.disconnectReasonName(reason),
              static_cast<int>(info.wifi_sta_disconnected.reason));
      break;
    }
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      LOG_INF("WIFI", "Station lost IP");
      break;
    default:
      break;
  }
}
}  // namespace

void WifiSelectionActivity::onEnter() {
  UiStatusActivity::onEnter();

  // Registered once for the life of the process; WiFi.onEvent keeps its own list
  // and re-entering this activity would otherwise log each event twice over.
  static bool wifiEventsRegistered = false;
  if (!wifiEventsRegistered) {
    WiFi.onEvent(logWifiStationEvent, ARDUINO_EVENT_WIFI_STA_CONNECTED);
    WiFi.onEvent(logWifiStationEvent, ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(logWifiStationEvent, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.onEvent(logWifiStationEvent, ARDUINO_EVENT_WIFI_STA_LOST_IP);
    wifiEventsRegistered = true;
  }

  // Load saved WiFi credentials - SD card operations need lock as we use SPI
  // for both
  {
    RenderLock lock(*this);
    WIFI_STORE.loadFromFile();
  }

  // Reset state
  setListSelection(0);
  networks.clear();
  rows.clear();
  realNetworkCount = 0;
  state = WifiSelectionState::SCANNING;
  selectedSSID.clear();
  connectedIP.clear();
  connectionError.clear();
  enteredPassword.clear();
  scanPending = false;
  joinPending = false;

  // Read the hardware-derived station MAC directly. WiFi.macAddress() depends
  // on the STA netif already existing, but this screen is entered while WiFi
  // is often still off (notably after an X4 Pro WiFi session).
  uint8_t mac[6] = {};
  char macStr[64];
  const esp_err_t macResult = esp_read_mac(mac, ESP_MAC_WIFI_STA);
  if (macResult == ESP_OK) {
    snprintf(macStr, sizeof(macStr), "%s %02x-%02x-%02x-%02x-%02x-%02x", tr(STR_MAC_ADDRESS), mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
  } else {
    LOG_ERR("WIFI", "Failed to read station MAC (err=%d)", static_cast<int>(macResult));
    snprintf(macStr, sizeof(macStr), "%s --", tr(STR_MAC_ADDRESS));
  }
  cachedMacAddress = std::string(macStr);

  // Trigger first update to show scanning message
  requestUpdate();

  // The session decides whether to try a saved network first or go straight to
  // scanning; both are just actions this activity carries out.
  wifi_session::Startup startup;
  startup.allowAutoConnect = allowAutoConnect;
  for (const WifiCredentialSummary& saved : WIFI_STORE.getCredentialSummaries()) {
    startup.savedSsids.push_back(saved.ssid);
  }
  startup.lastConnectedSsid = WIFI_STORE.getLastConnectedSsid();
  session.begin(startup, millis());
  pumpSession();
}

void WifiSelectionActivity::onExit() {
  Activity::onExit();

  LOG_DBG("WIFI", "Free heap at onExit start: %d bytes", ESP.getFreeHeap());

  // Stop any ongoing WiFi scan
  LOG_DBG("WIFI", "Deleting WiFi scan...");
  WiFi.scanDelete();
  LOG_DBG("WIFI", "Free heap after scanDelete: %d bytes", ESP.getFreeHeap());

  // Note: We do NOT disconnect WiFi here - the parent activity
  // (CrossPointWebServerActivity) manages WiFi connection state. We just clean
  // up the scan and task.

  LOG_DBG("WIFI", "Free heap at onExit end: %d bytes", ESP.getFreeHeap());
}

void WifiSelectionActivity::startWifiScan() {
  networks.clear();
  rows.clear();
  realNetworkCount = 0;
  setListSelection(0);
  requestUpdate();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  WiFi.scanNetworks(true);  // true = async scan
  scanPending = true;
}

void WifiSelectionActivity::appendHiddenNetworkEntry() {
  // Synthetic list entry that lets the user type an SSID that is not broadcast.
  // ESP32 can join hidden APs as long as the SSID is supplied to WiFi.begin().
  WifiNetworkInfo placeholder;
  placeholder.rssi = 0;
  placeholder.isEncrypted = true;  // Treated as encrypted; an empty password still connects open APs
  placeholder.hasSavedPassword = false;
  placeholder.isHiddenPlaceholder = true;
  networks.push_back(std::move(placeholder));
}

void WifiSelectionActivity::rebuildNetworkView() {
  // The session owns the ordering; this is the drawable copy of it, plus the
  // synthetic row that the reader types a hidden SSID into.
  networks.clear();
  for (const wifi_session::Network& network : session.networks()) {
    WifiNetworkInfo row;
    row.ssid = network.ssid;
    row.rssi = network.rssi;
    row.isEncrypted = network.isEncrypted;
    row.hasSavedPassword = network.hasSavedPassword;
    networks.push_back(std::move(row));
  }
  realNetworkCount = networks.size();
  appendHiddenNetworkEntry();
  if (listSelection() >= static_cast<int>(networks.size())) setListSelection(0);
  refreshRows();
  refreshHeaderCount();
}

void WifiSelectionActivity::beginJoin(const std::string& ssid, const std::string& password) {
  selectedSSID = ssid;
  connectedIP.clear();
  connectionError.clear();
  requestUpdate();

  WiFi.persistent(false);  // Credentials are managed by WifiCredentialStore; suppress SDK NVS auto-connect
  WiFi.mode(WIFI_STA);
  // Abort any in-progress SDK auto-connect, but leave the stored AP config alone
  // and keep the radio up: erasing the config and power-cycling the radio makes
  // some routers fail the WPA handshake that follows, which showed as a reader
  // that would not join a network it had joined the day before.
  if (!WiFi.disconnect(false, false, 1000)) {
    LOG_DBG("WIFI", "Disconnect before begin timed out; continuing with explicit begin");
  }
  delay(100);

  // Scan all channels so networks with multiple APs use the strongest matching
  // BSSID instead of the first match found by the framework's default fast scan.
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  // Set hostname so routers show "CrossPoint-Reader-AABBCCDDEEFF" instead of "esp32-XXXXXXXXXXXX"
  uint8_t mac[6] = {};
  const esp_err_t macResult = esp_read_mac(mac, ESP_MAC_WIFI_STA);
  if (macResult == ESP_OK) {
    char hostname[sizeof("CrossPoint-Reader-") + 12];
    snprintf(hostname, sizeof(hostname), "CrossPoint-Reader-%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5]);
    WiFi.setHostname(hostname);
  } else {
    LOG_ERR("WIFI", "Failed to read station MAC for hostname (err=%d)", static_cast<int>(macResult));
  }

  // Length only, never the password itself: enough to tell a truncated or empty
  // stored credential from a wrong one, without putting a secret in a log that
  // gets mailed around.
  LOG_INF("WIFI", "Joining: ssid_len=%u password_len=%u", static_cast<unsigned>(ssid.size()),
          static_cast<unsigned>(password.size()));

  if (!password.empty()) {
    WiFi.begin(ssid.c_str(), password.c_str());
  } else {
    WiFi.begin(ssid.c_str());
  }
  joinPending = true;
}

void WifiSelectionActivity::runAction(const wifi_session::Action& action) {
  switch (action.kind) {
    case wifi_session::ActionKind::START_SCAN:
      startWifiScan();
      return;

    case wifi_session::ActionKind::JOIN: {
      std::string password = enteredPassword;
      if (action.useSavedPassword) {
        const auto saved = WIFI_STORE.findCredential(action.ssid);
        password = saved ? saved->password : std::string();
        enteredPassword.clear();
      }
      beginJoin(action.ssid, password);
      return;
    }

    case wifi_session::ActionKind::DISCONNECT:
      WiFi.disconnect();
      joinPending = false;
      return;

    case wifi_session::ActionKind::ASK_FOR_PASSWORD:
      selectedSSID = action.ssid;
      promptPasswordEntry();
      return;

    case wifi_session::ActionKind::SAVE_CREDENTIAL: {
      // SD card operations need the lock as we use SPI for both.
      RenderLock lock(*this);
      WIFI_STORE.addCredential(action.ssid, enteredPassword);
      return;
    }

    case wifi_session::ActionKind::FORGET_CREDENTIAL: {
      RenderLock lock(*this);
      WIFI_STORE.removeCredential(action.ssid);
      return;
    }

    case wifi_session::ActionKind::FINISH:
      onComplete(action.connected);
      return;
  }
}

bool WifiSelectionActivity::promptIsOpen() const {
  return state == WifiSelectionState::PASSWORD_ENTRY || state == WifiSelectionState::HIDDEN_SSID_ENTRY ||
         state == WifiSelectionState::FORGET_PROMPT;
}

void WifiSelectionActivity::syncStateFromSession() {
  if (promptIsOpen()) {
    return;
  }

  WifiSelectionState next = state;
  switch (session.state()) {
    case wifi_session::State::AUTO_CONNECTING:
      next = scanPending ? WifiSelectionState::SCANNING : WifiSelectionState::AUTO_CONNECTING;
      break;
    case wifi_session::State::SCANNING:
      next = WifiSelectionState::SCANNING;
      break;
    case wifi_session::State::NETWORK_LIST:
      next = WifiSelectionState::NETWORK_LIST;
      break;
    case wifi_session::State::CONNECTING:
      next = WifiSelectionState::CONNECTING;
      break;
    case wifi_session::State::CONNECTED:
      next = session.offersToSaveCredential() ? WifiSelectionState::SAVE_PROMPT : WifiSelectionState::CONNECTED;
      break;
    case wifi_session::State::FAILED:
      next = WifiSelectionState::CONNECTION_FAILED;
      break;
  }

  if (next == state) {
    return;
  }
  // Both prompts open on their safe answer: save the password, keep the network.
  if (next == WifiSelectionState::SAVE_PROMPT) setChoiceIndex(0);
  state = next;
  if (next == WifiSelectionState::CONNECTED || next == WifiSelectionState::SAVE_PROMPT ||
      next == WifiSelectionState::CONNECTING) {
    refreshConnectionLines();
  }
  requestUpdate();
}

void WifiSelectionActivity::pumpSession() {
  wifi_session::Action action;
  while (session.nextAction(millis(), action)) {
    runAction(action);
    if (action.kind == wifi_session::ActionKind::FINISH) {
      return;  // The activity is finishing; nothing after this is ours to touch.
    }
  }
  syncStateFromSession();
}

void WifiSelectionActivity::pollRadio() {
  if (scanPending) {
    const int16_t scanResult = WiFi.scanComplete();
    if (scanResult == WIFI_SCAN_RUNNING) {
      return;
    }

    scanPending = false;
    if (scanResult == WIFI_SCAN_FAILED) {
      session.onScanFailed(millis());
      rebuildNetworkView();
      requestUpdate();
      return;
    }

    std::vector<wifi_session::Network> found;
    found.reserve(scanResult);
    for (int i = 0; i < scanResult; i++) {
      wifi_session::Network network;
      strlcpy(network.ssid, WiFi.SSID(i).c_str(), sizeof(network.ssid));
      // Skip hidden networks (empty SSID); the synthetic row covers those.
      if (network.ssid[0] == '\0') {
        continue;
      }
      network.rssi = WiFi.RSSI(i);
      const wifi_auth_mode_t authMode = WiFi.encryptionType(i);
      network.isEncrypted = (authMode != WIFI_AUTH_OPEN);
      // A join that dies in the 4-way handshake looks identical whether the saved
      // password is stale or the access point demands something this radio will not
      // do (WPA3-only, or PMF required). The auth mode separates the two, and it is
      // only knowable here, while the scan results are still alive.
      LOG_INF("WIFI", "Scan: auth=%d rssi=%d channel=%d", static_cast<int>(authMode), static_cast<int>(network.rssi),
              static_cast<int>(WiFi.channel(i)));
      found.push_back(network);
    }
    WiFi.scanDelete();

    session.onScanResults(found.data(), found.size(), millis());
    rebuildNetworkView();
    requestUpdate();
    return;
  }

  if (!joinPending) {
    return;
  }

  const wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    joinPending = false;
    IPAddress ip = WiFi.localIP();
    char ipStr[16];
    snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    connectedIP = ipStr;

#if defined(ENABLE_SERIAL_LOG) && LOG_LEVEL >= 2
    uint8_t connectedBssid[6] = {};
    WiFi.BSSID(connectedBssid);
    LOG_DBG("WIFI", "Connected BSSID: %02x:%02x:%02x:%02x:%02x:%02x, channel: %d, RSSI: %d dBm",
            static_cast<unsigned>(connectedBssid[0]), static_cast<unsigned>(connectedBssid[1]),
            static_cast<unsigned>(connectedBssid[2]), static_cast<unsigned>(connectedBssid[3]),
            static_cast<unsigned>(connectedBssid[4]), static_cast<unsigned>(connectedBssid[5]), WiFi.channel(),
            WiFi.RSSI());
#endif

    // With an RTC, sync on the first successful WiFi connection only. The DS3231 drifts
    // ~2 ppm so one sync is enough; users can force a re-sync from Settings > Customise
    // Status Bar > Sync clock now.
    //
    // Without one, the system clock is all reading stats have to date a session by, and
    // it is lost every time the battery latch drops. So it is re-synced on any connect
    // that finds it unset, and clockHasBeenSynced stays untouched: nothing was retained.
    const bool needsSync = halClock.isAvailable() ? !SETTINGS.clockHasBeenSynced : !halClock.hasDate();
    if (needsSync && halClock.syncFromNTP() && halClock.isAvailable()) {
      SETTINGS.clockHasBeenSynced = 1;
      SETTINGS.saveToFile();
    }

    {
      RenderLock lock(*this);
      WIFI_STORE.setLastConnectedSsid(session.activeSsid());
    }

    session.onJoinSucceeded(millis());
    return;
  }

  if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
    joinPending = false;
    connectionError = status == WL_NO_SSID_AVAIL ? tr(STR_ERROR_NETWORK_NOT_FOUND) : tr(STR_ERROR_GENERAL_FAILURE);
    session.onJoinFailed(millis());
  }
}

void WifiSelectionActivity::promptPasswordEntry() {
  // Show password entry
  state = WifiSelectionState::PASSWORD_ENTRY;
  // Don't allow screen updates while changing activity
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_ENTER_WIFI_PASSWORD),
                                                                 "",  // No initial text
                                                                 64,  // Max password length
                                                                 InputType::Password),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             state = WifiSelectionState::NETWORK_LIST;
                             session.abandonPasswordEntry(millis());
                           } else {
                             enteredPassword = std::get<KeyboardResult>(result.data).text;
                             // state will be updated in next loop iteration
                           }
                         });
}

void WifiSelectionActivity::promptHiddenSsid() {
  selectedSSID.clear();
  enteredPassword.clear();

  // Suppress rendering during the activity transition (see render()).
  state = WifiSelectionState::HIDDEN_SSID_ENTRY;
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_ENTER_WIFI_SSID),
                                                                 "",  // No initial text
                                                                 32,  // Max SSID length (IEEE 802.11: 32 bytes)
                                                                 InputType::Text),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             state = WifiSelectionState::NETWORK_LIST;
                             session.abandonPasswordEntry(millis());
                             return;
                           }
                           selectedSSID = std::get<KeyboardResult>(result.data).text;
                           if (selectedSSID.empty()) {
                             state = WifiSelectionState::NETWORK_LIST;
                             session.abandonPasswordEntry(millis());
                           }
                           // Otherwise stay in HIDDEN_SSID_ENTRY; loop() continues the flow.
                         });
}

std::string WifiSelectionActivity::getSignalStrengthIndicator(const int32_t rssi) const {
  // Convert RSSI to signal bars representation
  if (rssi >= -50) {
    return "||||";  // Excellent
  }
  if (rssi >= -60) {
    return " |||";  // Good
  }
  if (rssi >= -70) {
    return "  ||";  // Fair
  }
  return "   |";  // Very weak
}

void WifiSelectionActivity::onComplete(const bool connected) {
  ActivityResult result;
  result.isCancelled = !connected;
  if (connected) {
    result.data = WifiResult{true, selectedSSID, connectedIP};
  }
  setResult(std::move(result));
  finish();
}

// --- Rows and lines ---

void WifiSelectionActivity::refreshRows() {
  rowLabels.clear();
  rowValues.clear();
  rows.clear();
  rowLabels.reserve(networks.size());
  rowValues.reserve(networks.size());
  for (const WifiNetworkInfo& network : networks) {
    if (network.isHiddenPlaceholder) {
      rowLabels.emplace_back(tr(STR_ADD_HIDDEN_NETWORK));
      rowValues.emplace_back();
      continue;
    }
    rowLabels.push_back(network.ssid);
    // The legend under the list says what these mean: a plus for a password
    // already saved, a star for an encrypted network, then the signal.
    rowValues.push_back(std::string(network.hasSavedPassword ? "+ " : "") + (network.isEncrypted ? "* " : "") +
                        getSignalStrengthIndicator(network.rssi));
  }

  // Second pass: the strings must stop moving before their addresses are taken.
  rows.resize(networks.size());
  for (size_t i = 0; i < networks.size(); ++i) {
    rows[i] = freeink::ui::ListItem{};
    rows[i].label = rowLabels[i].c_str();
    rows[i].actionValue = static_cast<int16_t>(i);
    if (!rowValues[i].empty()) rows[i].value = rowValues[i].c_str();
  }
}

void WifiSelectionActivity::refreshHeaderCount() {
  // STR_NETWORKS_FOUND is ~37 bytes once the Arabic translation is substituted,
  // so 32 truncated it. See ClockSyncActivity for the same class of bug.
  char countStr[64];
  snprintf(countStr, sizeof(countStr), tr(STR_NETWORKS_FOUND), realNetworkCount);
  networkCountLine = countStr;
}

void WifiSelectionActivity::refreshConnectionLines() {
  ssidLine =
      std::string(state == WifiSelectionState::CONNECTING ? tr(STR_TO_PREFIX) : tr(STR_NETWORK_PREFIX)) + selectedSSID;
  ipLine = std::string(tr(STR_IP_ADDRESS_PREFIX)) + connectedIP;
}

// --- Screen ---

UiStatusActivity::StatusView WifiSelectionActivity::statusView() const {
  StatusView view;
  view.title = tr(STR_WIFI_NETWORKS);
  view.headerRight = networkCountLine.c_str();
  view.subtitleLeft = cachedMacAddress.c_str();

  // Automatic attempts get their own wording and offer a way out of them.
  const bool autoConnecting = session.state() == wifi_session::State::AUTO_CONNECTING;

  switch (state) {
    case WifiSelectionState::HIDDEN_SSID_ENTRY:
    case WifiSelectionState::PASSWORD_ENTRY:
      // The keyboard owns the screen.
      view.hidden = true;
      break;
    case WifiSelectionState::SCANNING:
      view.lines = {autoConnecting ? tr(STR_FINDING_SAVED_WIFI) : tr(STR_SCANNING), nullptr, nullptr, nullptr};
      view.backHint = autoConnecting ? tr(STR_CANCEL) : "";
      view.confirmHint = autoConnecting ? tr(STR_SHOW_NETWORKS) : "";
      break;
    case WifiSelectionState::AUTO_CONNECTING:
    case WifiSelectionState::CONNECTING:
      view.lines = {autoConnecting ? tr(STR_CONNECTING_SAVED_WIFI) : tr(STR_CONNECTING), ssidLine.c_str(), nullptr,
                    nullptr};
      view.backHint = autoConnecting ? tr(STR_CANCEL) : "";
      view.confirmHint = autoConnecting ? tr(STR_SHOW_NETWORKS) : "";
      break;
    case WifiSelectionState::NETWORK_LIST:
      if (rows.empty()) {
        view.lines = {tr(STR_NO_NETWORKS), tr(STR_PRESS_OK_SCAN), nullptr, nullptr};
        view.confirmHint = tr(STR_RETRY);
        break;
      }
      view.listItems = rows.data();
      view.listCount = static_cast<int>(rows.size());
      view.listNote = tr(STR_NETWORK_LEGEND);
      view.confirmHint = tr(STR_CONNECT);
      // Left forgets, right rescans, the same two the list has always bound.
      view.thirdHint =
          listSelection() < static_cast<int>(realNetworkCount) && networks[listSelection()].hasSavedPassword
              ? tr(STR_FORGET_BUTTON)
              : "";
      view.fourthHint = tr(STR_RETRY);
      break;
    case WifiSelectionState::CONNECTED:
      view.lines = {tr(STR_CONNECTED), ssidLine.c_str(), ipLine.c_str(), nullptr};
      view.backHint = "";
      view.confirmHint = tr(STR_DONE);
      break;
    case WifiSelectionState::SAVE_PROMPT:
      view.lines = {tr(STR_CONNECTED), ssidLine.c_str(), tr(STR_SAVE_PASSWORD), nullptr};
      view.choices = {tr(STR_YES), tr(STR_NO)};
      view.backHint = tr(STR_CANCEL);
      view.confirmHint = tr(STR_SELECT);
      break;
    case WifiSelectionState::FORGET_PROMPT:
      view.lines = {tr(STR_FORGET_NETWORK), ssidLine.c_str(), tr(STR_FORGET_AND_REMOVE), nullptr};
      view.choices = {tr(STR_CANCEL), tr(STR_FORGET_BUTTON)};
      view.confirmHint = tr(STR_SELECT);
      break;
    case WifiSelectionState::CONNECTION_FAILED:
      view.lines = {tr(STR_CONNECTION_FAILED), connectionError.c_str(), nullptr, nullptr};
      view.confirmHint = tr(STR_DONE);
      // A stored password that no longer opens the door is the one failure worth
      // a way out of here (#114); a typed one is simply retyped from the list.
      view.thirdHint = session.failedNetworkHasCredential() ? tr(STR_FORGET_BUTTON) : "";
      break;
  }
  return view;
}

// --- Input handling ---

bool WifiSelectionActivity::handleListSideButtons() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    session.rescan(millis());
    pumpSession();
    return true;
  }
  if (!mappedInput.wasPressed(MappedInputManager::Button::Left)) return false;
  const int selected = listSelection();
  if (selected >= static_cast<int>(realNetworkCount) || !networks[selected].hasSavedPassword) return false;
  selectedSSID = networks[selected].ssid;
  refreshConnectionLines();
  state = WifiSelectionState::FORGET_PROMPT;
  setChoiceIndex(0);  // Default to "Cancel"
  requestUpdate();
  return true;
}

bool WifiSelectionActivity::handleCustomInput() {
  // Reached once the hidden-network SSID has been entered (and was non-empty).
  if (state == WifiSelectionState::HIDDEN_SSID_ENTRY) {
    state = WifiSelectionState::NETWORK_LIST;
    session.selectHiddenNetwork(selectedSSID, millis());
    pumpSession();
    return true;
  }

  if (state == WifiSelectionState::PASSWORD_ENTRY) {
    // Reached once password entry finished in the subactivity.
    state = WifiSelectionState::NETWORK_LIST;
    session.onPasswordEntered(millis());
    pumpSession();
    return true;
  }

  if (state == WifiSelectionState::CONNECTED) {
    // Safety fallback - immediately complete
    onComplete(true);
    return true;
  }

  if (state == WifiSelectionState::NETWORK_LIST) {
    if (handleListSideButtons()) return true;
    pumpSession();
    return false;  // the base moves the selection and routes the taps
  }

  if (state == WifiSelectionState::SAVE_PROMPT || state == WifiSelectionState::FORGET_PROMPT) {
    return false;  // the base owns the two answers
  }

  if (state == WifiSelectionState::CONNECTION_FAILED && session.failedNetworkHasCredential() &&
      mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    forgetPromptForFailedJoin = true;
    refreshConnectionLines();
    state = WifiSelectionState::FORGET_PROMPT;
    setChoiceIndex(0);  // Default to "Cancel"
    requestUpdate();
    return true;
  }

  pollRadio();
  pumpSession();
  return false;
}

void WifiSelectionActivity::onListActivated(const int index) {
  if (state != WifiSelectionState::NETWORK_LIST) return;
  setListSelection(index);
  if (index < static_cast<int>(networks.size()) && networks[index].isHiddenPlaceholder) {
    promptHiddenSsid();
    return;
  }
  if (index < static_cast<int>(realNetworkCount)) {
    enteredPassword.clear();
    session.selectNetwork(index, millis());
    pumpSession();
    return;
  }
  session.rescan(millis());
  pumpSession();
}

void WifiSelectionActivity::onChoiceActivated(const int index) {
  if (state == WifiSelectionState::SAVE_PROMPT) {
    session.answerSavePrompt(index == 0, millis());
    pumpSession();
    return;
  }
  if (state != WifiSelectionState::FORGET_PROMPT) return;
  state = WifiSelectionState::NETWORK_LIST;
  const bool fromFailedJoin = forgetPromptForFailedJoin;
  forgetPromptForFailedJoin = false;
  if (index == 1) {
    if (fromFailedJoin) {
      session.forgetFailedNetwork(millis());
    } else {
      session.forgetNetwork(listSelection(), millis());
    }
  } else if (fromFailedJoin) {
    session.dismissFailure(millis());
  } else {
    session.rescan(millis());
  }
  pumpSession();
}

void WifiSelectionActivity::onBackButton() {
  switch (state) {
    case WifiSelectionState::SAVE_PROMPT:
      // Skip saving, complete anyway.
      session.answerSavePrompt(false, millis());
      pumpSession();
      return;
    case WifiSelectionState::FORGET_PROMPT:
      // Keep the network, go back to the list.
      state = WifiSelectionState::NETWORK_LIST;
      if (forgetPromptForFailedJoin) {
        forgetPromptForFailedJoin = false;
        session.dismissFailure(millis());
      } else {
        session.rescan(millis());
      }
      pumpSession();
      return;
    case WifiSelectionState::SCANNING:
    case WifiSelectionState::AUTO_CONNECTING:
      WiFi.scanDelete();
      WiFi.disconnect();
      session.cancel(millis());
      pumpSession();
      return;
    case WifiSelectionState::CONNECTION_FAILED:
      session.dismissFailure(millis());
      pumpSession();
      return;
    case WifiSelectionState::NETWORK_LIST:
      session.cancel(millis());
      pumpSession();
      return;
    default:
      return;
  }
}

void WifiSelectionActivity::onConfirmButton() {
  if (state == WifiSelectionState::SCANNING || state == WifiSelectionState::AUTO_CONNECTING) {
    // The reader took over; stop trying saved networks behind their back.
    session.showNetworkList(millis());
    pumpSession();
    return;
  }
  if (state == WifiSelectionState::CONNECTION_FAILED) {
    session.dismissFailure(millis());
    pumpSession();
    return;
  }
  if (state == WifiSelectionState::NETWORK_LIST && rows.empty()) {
    session.rescan(millis());
    pumpSession();
  }
}
