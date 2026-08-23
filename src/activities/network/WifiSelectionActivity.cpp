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
#include "components/UITheme.h"
#include "fontIds.h"

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
  Activity::onEnter();

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
  selectedNetworkIndex = 0;
  networks.clear();
  realNetworkCount = 0;
  state = WifiSelectionState::SCANNING;
  selectedSSID.clear();
  connectedIP.clear();
  connectionError.clear();
  enteredPassword.clear();
  savePromptSelection = 0;
  forgetPromptSelection = 0;
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
  realNetworkCount = 0;
  selectedNetworkIndex = 0;
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
  if (selectedNetworkIndex >= networks.size()) {
    selectedNetworkIndex = 0;
  }
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
  if (next == WifiSelectionState::SAVE_PROMPT) {
    savePromptSelection = 0;  // Default to "Yes"
  }
  state = next;
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
      network.isEncrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
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

    // Sync RTC from NTP on the first successful WiFi connection only. The DS3231
    // drifts ~2 ppm so one sync is enough; users can force a re-sync from
    // Settings > Customise Status Bar > Sync clock now.
    if (halClock.isAvailable() && !SETTINGS.clockHasBeenSynced) {
      if (halClock.syncFromNTP()) {
        SETTINGS.clockHasBeenSynced = 1;
        SETTINGS.saveToFile();
      }
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

void WifiSelectionActivity::handleNetworkListInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    session.cancel(millis());
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedNetworkIndex < networks.size() && networks[selectedNetworkIndex].isHiddenPlaceholder) {
      promptHiddenSsid();
      return;
    }
    if (selectedNetworkIndex < realNetworkCount) {
      enteredPassword.clear();
      session.selectNetwork(selectedNetworkIndex, millis());
      return;
    }
    session.rescan(millis());
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    session.rescan(millis());
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    if (selectedNetworkIndex < realNetworkCount && networks[selectedNetworkIndex].hasSavedPassword) {
      selectedSSID = networks[selectedNetworkIndex].ssid;
      state = WifiSelectionState::FORGET_PROMPT;
      forgetPromptSelection = 0;  // Default to "Cancel"
      requestUpdate();
      return;
    }
  }

  buttonNavigator.onNext([this] {
    selectedNetworkIndex = ButtonNavigator::nextIndex(selectedNetworkIndex, networks.size());
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedNetworkIndex = ButtonNavigator::previousIndex(selectedNetworkIndex, networks.size());
    requestUpdate();
  });
}

void WifiSelectionActivity::loop() {
  // Reached once the hidden-network SSID has been entered (and was non-empty).
  if (state == WifiSelectionState::HIDDEN_SSID_ENTRY) {
    state = WifiSelectionState::NETWORK_LIST;
    session.selectHiddenNetwork(selectedSSID, millis());
    pumpSession();
    return;
  }

  if (state == WifiSelectionState::PASSWORD_ENTRY) {
    // Reach here once password entry finished in subactivity
    state = WifiSelectionState::NETWORK_LIST;
    session.onPasswordEntered(millis());
    pumpSession();
    return;
  }

  if (state == WifiSelectionState::SAVE_PROMPT) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (savePromptSelection > 0) {
        savePromptSelection--;
        requestUpdate();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
               mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (savePromptSelection < 1) {
        savePromptSelection++;
        requestUpdate();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      session.answerSavePrompt(savePromptSelection == 0, millis());
      pumpSession();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      // Skip saving, complete anyway
      session.answerSavePrompt(false, millis());
      pumpSession();
    }
    return;
  }

  if (state == WifiSelectionState::FORGET_PROMPT) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (forgetPromptSelection > 0) {
        forgetPromptSelection--;
        requestUpdate();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
               mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (forgetPromptSelection < 1) {
        forgetPromptSelection++;
        requestUpdate();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      state = WifiSelectionState::NETWORK_LIST;
      if (forgetPromptSelection == 1) {
        session.forgetNetwork(selectedNetworkIndex, millis());
      } else {
        session.rescan(millis());
      }
      pumpSession();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      // Skip forgetting, go back to network list
      state = WifiSelectionState::NETWORK_LIST;
      session.rescan(millis());
      pumpSession();
    }
    return;
  }

  if (state == WifiSelectionState::SCANNING || state == WifiSelectionState::AUTO_CONNECTING) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      WiFi.scanDelete();
      WiFi.disconnect();
      session.cancel(millis());
      pumpSession();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      // The reader took over; stop trying saved networks behind their back.
      session.showNetworkList(millis());
      pumpSession();
      return;
    }
  }

  if (state == WifiSelectionState::CONNECTED) {
    // Safety fallback - immediately complete
    onComplete(true);
    return;
  }

  if (state == WifiSelectionState::CONNECTION_FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      session.dismissFailure(millis());
      pumpSession();
      return;
    }
  }

  if (state == WifiSelectionState::NETWORK_LIST) {
    handleNetworkListInput();
    pumpSession();
    return;
  }

  pollRadio();
  pumpSession();
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

void WifiSelectionActivity::render(RenderLock&&) {
  // Don't render if we're in a keyboard-entry state - we're just transitioning
  // from the keyboard subactivity back to the main activity
  if (state == WifiSelectionState::PASSWORD_ENTRY || state == WifiSelectionState::HIDDEN_SSID_ENTRY) {
    return;
  }

  renderer.clearScreen();

  auto& theme = UITheme::getInstance();
  auto metrics = theme.getMetrics();
  Rect screen = theme.getScreenSafeArea(renderer, true, false);

  // Draw header
  // STR_NETWORKS_FOUND is ~37 bytes once the Arabic translation is substituted,
  // so 32 truncated it. See ClockSyncActivity for the same class of bug.
  char countStr[64];
  snprintf(countStr, sizeof(countStr), tr(STR_NETWORKS_FOUND), realNetworkCount);
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_WIFI_NETWORKS), countStr);
  GUI.drawSubHeader(
      renderer,
      Rect{screen.x, screen.y + metrics.topPadding + metrics.headerHeight, screen.width, metrics.tabBarHeight},
      cachedMacAddress.c_str());

  switch (state) {
    case WifiSelectionState::AUTO_CONNECTING:
      renderConnecting(&screen, &metrics);
      break;
    case WifiSelectionState::SCANNING:
      renderConnecting(&screen, &metrics);  // Reuse connecting screen with different message
      break;
    case WifiSelectionState::NETWORK_LIST:
      renderNetworkList(&screen, &metrics);
      break;
    case WifiSelectionState::HIDDEN_SSID_ENTRY:
      // Transitioning to/from the SSID keyboard subactivity - nothing to draw
      break;
    case WifiSelectionState::CONNECTING:
      renderConnecting(&screen, &metrics);
      break;
    case WifiSelectionState::CONNECTED:
      renderConnected(&screen, &metrics);
      break;
    case WifiSelectionState::SAVE_PROMPT:
      renderSavePrompt(&screen, &metrics);
      break;
    case WifiSelectionState::CONNECTION_FAILED:
      renderConnectionFailed(&screen, &metrics);
      break;
    case WifiSelectionState::FORGET_PROMPT:
      renderForgetPrompt(&screen, &metrics);
      break;
  }

  renderer.displayBuffer();
}

void WifiSelectionActivity::renderNetworkList(const Rect* screen, const ThemeMetrics* metrics) const {
  if (networks.empty()) {
    // No networks found or scan failed
    const auto height = renderer.getLineHeight(UI_10_FONT_ID);
    const auto top = screen->y + (screen->height - height) / 2;
    UITheme::drawCenteredText(renderer, *screen, UI_10_FONT_ID, top, tr(STR_NO_NETWORKS));
    UITheme::drawCenteredText(renderer, *screen, SMALL_FONT_ID, top + height + 10, tr(STR_PRESS_OK_SCAN));
  } else {
    int contentTop =
        screen->y + metrics->topPadding + metrics->headerHeight + metrics->tabBarHeight + metrics->verticalSpacing;
    int contentHeight = screen->height - contentTop - metrics->verticalSpacing * 2;
    GUI.drawList(
        renderer, Rect{screen->x, contentTop, screen->width, contentHeight}, static_cast<int>(networks.size()),
        selectedNetworkIndex,
        [this](int index) {
          const auto& network = networks[index];
          return network.isHiddenPlaceholder ? std::string(tr(STR_ADD_HIDDEN_NETWORK)) : network.ssid;
        },
        nullptr, nullptr,
        [this](int index) {
          const auto& network = networks[index];
          if (network.isHiddenPlaceholder) {
            return std::string();
          }
          return std::string(network.hasSavedPassword ? "+ " : "") + (network.isEncrypted ? "* " : "") +
                 getSignalStrengthIndicator(network.rssi);
        });
  }

  GUI.drawHelpText(renderer,
                   Rect{screen->x, screen->y + screen->height - metrics->contentSidePadding - 15, screen->width, 20},
                   tr(STR_NETWORK_LEGEND));

  const bool hasSavedPassword = !networks.empty() && networks[selectedNetworkIndex].hasSavedPassword;
  const char* forgetLabel = hasSavedPassword ? tr(STR_FORGET_BUTTON) : "";

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONNECT), forgetLabel, tr(STR_RETRY));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void WifiSelectionActivity::renderConnecting(const Rect* screen, const ThemeMetrics* metrics) const {
  constexpr int MAX_STATUS_LINES = 2;
  // Automatic attempts get their own wording and offer a way out of them.
  const bool autoConnecting = session.state() == wifi_session::State::AUTO_CONNECTING;
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = screen->y + (screen->height - height) / 2;
  const int statusX = screen->x + metrics->contentSidePadding;
  const int statusWidth = screen->width - metrics->contentSidePadding * 2;

  if (state == WifiSelectionState::SCANNING) {
    const char* statusText = autoConnecting ? tr(STR_FINDING_SAVED_WIFI) : tr(STR_SCANNING);
    const Rect statusBounds{statusX, screen->y, statusWidth, screen->height};
    UITheme::drawCenteredWrappedText(renderer, statusBounds, UI_10_FONT_ID, statusText, MAX_STATUS_LINES);
    if (autoConnecting) {
      const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_SHOW_NETWORKS), "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }
  } else {
    const char* statusText = autoConnecting ? tr(STR_CONNECTING_SAVED_WIFI) : tr(STR_CONNECTING);
    const Rect statusBounds{statusX, screen->y, statusWidth, top - metrics->verticalSpacing - screen->y};
    UITheme::drawCenteredWrappedText(renderer, statusBounds, UI_12_FONT_ID, statusText, MAX_STATUS_LINES, true,
                                     EpdFontFamily::REGULAR, UITheme::TextVerticalAlignment::BOTTOM);

    std::string ssidInfo = std::string(tr(STR_TO_PREFIX)) + selectedSSID;
    if (ssidInfo.length() > 25) {
      ssidInfo.replace(22, ssidInfo.length() - 22, "...");
    }
    UITheme::drawCenteredText(renderer, *screen, UI_10_FONT_ID, top, ssidInfo.c_str());
    if (autoConnecting) {
      const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_SHOW_NETWORKS), "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }
  }
}

void WifiSelectionActivity::renderConnected(const Rect* screen, const ThemeMetrics* metrics) const {
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = screen->y + (screen->height - height * 4) / 2;

  UITheme::drawCenteredText(renderer, *screen, UI_12_FONT_ID, top - 30, tr(STR_CONNECTED), true,
                            EpdFontFamily::REGULAR);

  std::string ssidInfo = std::string(tr(STR_NETWORK_PREFIX)) + selectedSSID;
  if (ssidInfo.length() > 28) {
    ssidInfo.replace(25, ssidInfo.length() - 25, "...");
  }
  UITheme::drawCenteredText(renderer, *screen, UI_10_FONT_ID, top + 10, ssidInfo.c_str());

  const std::string ipInfo = std::string(tr(STR_IP_ADDRESS_PREFIX)) + connectedIP;
  UITheme::drawCenteredText(renderer, *screen, UI_10_FONT_ID, top + 40, ipInfo.c_str());

  // Use centralized button hints
  const auto labels = mappedInput.mapLabels("", tr(STR_DONE), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void WifiSelectionActivity::renderSavePrompt(const Rect* screen, const ThemeMetrics* metrics) const {
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = screen->y + (screen->height - height * 3) / 2;

  UITheme::drawCenteredText(renderer, *screen, UI_12_FONT_ID, top - 40, tr(STR_CONNECTED), true,
                            EpdFontFamily::REGULAR);

  std::string ssidInfo = std::string(tr(STR_NETWORK_PREFIX)) + selectedSSID;
  if (ssidInfo.length() > 28) {
    ssidInfo.replace(25, ssidInfo.length() - 25, "...");
  }
  UITheme::drawCenteredText(renderer, *screen, UI_10_FONT_ID, top, ssidInfo.c_str());

  UITheme::drawCenteredText(renderer, *screen, UI_10_FONT_ID, top + 40, tr(STR_SAVE_PASSWORD));

  // Draw Yes/No buttons
  const int buttonY = top + 80;
  constexpr int buttonWidth = 60;
  constexpr int buttonSpacing = 30;
  constexpr int totalWidth = buttonWidth * 2 + buttonSpacing;
  const int startX = screen->x + (screen->width - totalWidth) / 2;

  // Draw "Yes" button
  if (savePromptSelection == 0) {
    std::string text = "[" + std::string(tr(STR_YES)) + "]";
    renderer.drawText(UI_10_FONT_ID, startX, buttonY, text.c_str());
  } else {
    renderer.drawText(UI_10_FONT_ID, startX + 4, buttonY, tr(STR_YES));
  }

  // Draw "No" button
  if (savePromptSelection == 1) {
    std::string text = "[" + std::string(tr(STR_NO)) + "]";
    renderer.drawText(UI_10_FONT_ID, startX + buttonWidth + buttonSpacing, buttonY, text.c_str());
  } else {
    renderer.drawText(UI_10_FONT_ID, startX + buttonWidth + buttonSpacing + 4, buttonY, tr(STR_NO));
  }

  // Use centralized button hints
  const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void WifiSelectionActivity::renderConnectionFailed(const Rect* screen, const ThemeMetrics* metrics) const {
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = screen->y + (screen->height - height * 2) / 2;

  UITheme::drawCenteredText(renderer, *screen, UI_12_FONT_ID, top - 20, tr(STR_CONNECTION_FAILED), true,
                            EpdFontFamily::REGULAR);
  UITheme::drawCenteredText(renderer, *screen, UI_10_FONT_ID, top + 20, connectionError.c_str());

  // Use centralized button hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DONE), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void WifiSelectionActivity::renderForgetPrompt(const Rect* screen, const ThemeMetrics* metrics) const {
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = screen->y + (screen->height - height * 3) / 2;

  UITheme::drawCenteredText(renderer, *screen, UI_12_FONT_ID, top - 40, tr(STR_FORGET_NETWORK), true,
                            EpdFontFamily::REGULAR);

  std::string ssidInfo = std::string(tr(STR_NETWORK_PREFIX)) + selectedSSID;
  if (ssidInfo.length() > 28) {
    ssidInfo.replace(25, ssidInfo.length() - 25, "...");
  }
  UITheme::drawCenteredText(renderer, *screen, UI_10_FONT_ID, top, ssidInfo.c_str());

  UITheme::drawCenteredText(renderer, *screen, UI_10_FONT_ID, top + 40, tr(STR_FORGET_AND_REMOVE));

  // Draw Cancel/Forget network buttons
  const int buttonY = top + 80;
  constexpr int buttonWidth = 120;
  constexpr int buttonSpacing = 30;
  constexpr int totalWidth = buttonWidth * 2 + buttonSpacing;
  const int startX = screen->x + (screen->width - totalWidth) / 2;

  // Draw "Cancel" button
  if (forgetPromptSelection == 0) {
    std::string text = "[" + std::string(tr(STR_CANCEL)) + "]";
    renderer.drawText(UI_10_FONT_ID, startX, buttonY, text.c_str());
  } else {
    renderer.drawText(UI_10_FONT_ID, startX + 4, buttonY, tr(STR_CANCEL));
  }

  // Draw "Forget network" button
  if (forgetPromptSelection == 1) {
    std::string text = "[" + std::string(tr(STR_FORGET_BUTTON)) + "]";
    renderer.drawText(UI_10_FONT_ID, startX + buttonWidth + buttonSpacing, buttonY, text.c_str());
  } else {
    renderer.drawText(UI_10_FONT_ID, startX + buttonWidth + buttonSpacing + 4, buttonY, tr(STR_FORGET_BUTTON));
  }

  // Use centralized button hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
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
