#include "CalibreConnectActivity.h"

#include <ESPmDNS.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TaskWatchdog.h"

namespace {
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

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
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

  MDNS.end();
  if (MDNS.begin(HOSTNAME)) {
    // mDNS is optional for the Calibre plugin but still helpful for users.
    LOG_DBG("CAL", "mDNS started: http://%s.local/", HOSTNAME);
  }

  // Heap-critical allocation: SD-font caches retained for the CJK UI fallback
  // are rebuildable — release them (again: the WiFi selection screen may have
  // repopulated them rendering a CJK SSID) so the server object doesn't abort
  // on OOM. See CrossPointWebServerActivity::startWebServer().
  if (auto* fcm = renderer.getFontCacheManager()) {
    LOG_DBG("CAL", "Free heap before SD font cache release: %d bytes", ESP.getFreeHeap());
    fcm->releaseSdFontCaches();
    LOG_DBG("CAL", "Free heap before server alloc: %d bytes", ESP.getFreeHeap());
  }

  webServer = makeUniqueNoThrow<CrossPointWebServer>();
  if (!webServer) {
    LOG_ERR("CALIBRE", "OOM: CrossPointWebServer");
    return;
  }
  webServer->begin();

  if (webServer->isRunning()) {
    state = CalibreConnectState::SERVER_RUNNING;
    ipLine = std::string(tr(STR_IP_ADDRESS_PREFIX)) + connectedIP;
    requestUpdate();
  } else {
    state = CalibreConnectState::ERROR;
    requestUpdate();
  }
}

void CalibreConnectActivity::stopWebServer() {
  if (webServer) {
    webServer->stop();
    webServer.reset();
  }
}

bool CalibreConnectActivity::handleCustomInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    exitRequested = true;
  }

  if (webServer && webServer->isRunning()) {
    const unsigned long timeSinceLastHandleClient = millis() - lastHandleClientTime;
    if (lastHandleClientTime > 0 && timeSinceLastHandleClient > 100) {
      LOG_DBG("CAL", "WARNING: %lu ms gap since last handleClient", timeSinceLastHandleClient);
    }

    resetTaskWatchdogIfSubscribed();
    constexpr int MAX_ITERATIONS = 80;
    for (int i = 0; i < MAX_ITERATIONS && webServer->isRunning(); i++) {
      webServer->handleClient();
      if ((i & 0x07) == 0x07) {
        resetTaskWatchdogIfSubscribed();
      }
      if ((i & 0x0F) == 0x0F) {
        yield();
        if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
          exitRequested = true;
          break;
        }
      }
    }
    lastHandleClientTime = millis();

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
        transferLine = tr(STR_CALIBRE_RECEIVING);
        if (!currentUploadName.empty()) transferLine += ": " + currentUploadName;
      } else if (lastCompleteAt > 0) {
        transferLine = std::string(tr(STR_CALIBRE_RECEIVED)) + lastCompleteName;
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
