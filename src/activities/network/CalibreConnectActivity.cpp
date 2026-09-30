#include "CalibreConnectActivity.h"

#include <ESPmDNS.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "WebServerSession.h"
#include "WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

// "Receiving: name". The translations carry their own colon but disagree on the space
// after it ("Empfange:", "Reçu : "), so the label is trimmed and one space added here;
// with no name the colon goes too.
std::string labelledName(const char* label, const std::string& name) {
  std::string out(label);
  while (!out.empty() && out.back() == ' ') out.pop_back();
  if (name.empty()) {
    while (!out.empty() && (out.back() == ':' || out.back() == ' ')) out.pop_back();
    return out;
  }
  return out + " " + name;
}

constexpr const char* HOSTNAME = "crosspoint";
}  // namespace

void CalibreConnectActivity::onEnter() {
  UiStatusActivity::onEnter();

  requestUpdate();
  state = CalibreConnectState::WIFI_SELECTION;
  connectedIP.clear();
  connectedSSID.clear();
  lastHandleClientTime = 0;
  lastProgressReceived = 0;
  lastProgressTotal = 0;
  currentUploadName.clear();
  lastCompleteName.clear();
  lastCompleteAt = 0;
  lastProcessedCompleteAt = 0;
  exitRequested = false;

  if (WiFi.status() != WL_CONNECTED) {
    startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& wifi = std::get<WifiResult>(result.data);
                               connectedIP = wifi.ip;
                               connectedSSID = wifi.ssid;
                             }
                             onWifiSelectionComplete(!result.isCancelled);
                           });
  } else {
    connectedIP = WiFi.localIP().toString().c_str();
    connectedSSID = WiFi.SSID().c_str();
    startWebServer();
  }
}

void CalibreConnectActivity::onExit() {
  Activity::onExit();

  MDNS.end();
  teardownWifiAndRestart();
}

void CalibreConnectActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    finish();
    return;
  }

  startWebServer();
}

void CalibreConnectActivity::startWebServer() {
  state = CalibreConnectState::SERVER_STARTING;
  requestUpdate();

  // mDNS is optional for the Calibre plugin but still helpful for users.
  restartMdns(HOSTNAME, "CAL");

  webServer = startServer(renderer, mappedInput, "CAL");
  if (!webServer) return;

  if (webServer->isRunning()) {
    state = CalibreConnectState::SERVER_RUNNING;
    ipLine = std::string(tr(STR_IP_ADDRESS_PREFIX)) + connectedIP;
    requestUpdate();
  } else {
    state = CalibreConnectState::ERROR;
    requestUpdate();
  }
}

bool CalibreConnectActivity::handleCustomInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    exitRequested = true;
  }

  if (webServer && webServer->isRunning()) {
    if (pumpServer(*webServer, mappedInput, lastHandleClientTime, "CAL", 80, 8, 16)) {
      exitRequested = true;
    }

    const auto status = webServer->getWsUploadStatus();
    bool changed = false;
    if (status.inProgress) {
      if (status.received != lastProgressReceived || status.total != lastProgressTotal ||
          status.filename != currentUploadName) {
        lastProgressReceived = status.received;
        lastProgressTotal = status.total;
        currentUploadName = status.filename;
        changed = true;
      }
    } else if (lastProgressReceived != 0 || lastProgressTotal != 0) {
      lastProgressReceived = 0;
      lastProgressTotal = 0;
      currentUploadName.clear();
      changed = true;
    }
    // Only update lastCompleteAt if the server has a NEW value (not one we already processed)
    // This prevents restoring an old value after the 6s timeout clears it
    if (status.lastCompleteAt != 0 && status.lastCompleteAt != lastProcessedCompleteAt) {
      lastCompleteAt = status.lastCompleteAt;
      lastCompleteName = status.lastCompleteName;
      lastProcessedCompleteAt = status.lastCompleteAt;  // Mark this value as processed
      changed = true;
    }
    if (lastCompleteAt > 0 && (millis() - lastCompleteAt) >= 6000) {
      lastCompleteAt = 0;
      lastCompleteName.clear();
      // Note: we DON'T reset lastProcessedCompleteAt here, so we won't re-process the old server value
      changed = true;
    }
    if (changed) {
      // One line for whichever the status section is showing: the book coming
      // in, or the one that just landed.
      if (lastProgressTotal > 0 && lastProgressReceived <= lastProgressTotal) {
        transferLine = labelledName(tr(STR_CALIBRE_RECEIVING), currentUploadName);
      } else if (lastCompleteAt > 0) {
        transferLine = labelledName(tr(STR_CALIBRE_RECEIVED), lastCompleteName);
      } else {
        transferLine.clear();
      }
      requestUpdate();
    }
  }

  if (exitRequested) {
    finish();
    return true;
  }
  return false;
}

UiStatusActivity::StatusView CalibreConnectActivity::statusView() const {
  StatusView view;
  view.title = tr(STR_CALIBRE_WIRELESS);
  switch (state) {
    case CalibreConnectState::SERVER_STARTING:
      view.lines = {tr(STR_CALIBRE_STARTING), nullptr, nullptr, nullptr};
      break;
    case CalibreConnectState::ERROR:
      view.lines = {tr(STR_CONNECTION_FAILED), nullptr, nullptr, nullptr};
      break;
    case CalibreConnectState::SERVER_RUNNING:
      view.subtitleLeft = connectedSSID.c_str();
      view.subtitleRight = ipLine.c_str();
      view.sections[0] = Section{tr(STR_CALIBRE_SETUP),
                                 {tr(STR_CALIBRE_INSTRUCTION_1), tr(STR_CALIBRE_INSTRUCTION_2),
                                  tr(STR_CALIBRE_INSTRUCTION_3), tr(STR_CALIBRE_INSTRUCTION_4)}};
      view.sections[1] = Section{tr(STR_CALIBRE_STATUS), {}};
      if (lastProgressTotal > 0 && lastProgressReceived <= lastProgressTotal) {
        view.progressLabel = transferLine.c_str();
        view.showProgress = true;
        view.progressValue = static_cast<int>(lastProgressReceived);
        view.progressMax = static_cast<int>(lastProgressTotal);
      } else if (lastCompleteAt > 0 && (millis() - lastCompleteAt) < 6000) {
        // The last book landed: its name holds the status section on its own
        // for six seconds, with no bar under it.
        view.sections[1].lines[0] = transferLine.c_str();
      }
      view.backHint = tr(STR_EXIT);
      break;
    case CalibreConnectState::WIFI_SELECTION:
      // The WiFi picker is a separate activity and owns the screen.
      break;
  }
  return view;
}
