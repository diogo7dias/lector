#pragma once

#include <functional>
#include <memory>
#include <string>

#include "activities/UiStatusActivity.h"
#include "network/CrossPointWebServer.h"

enum class CalibreConnectState { WIFI_SELECTION, SERVER_STARTING, SERVER_RUNNING, ERROR };

/**
 * CalibreConnectActivity starts the file transfer server in STA mode,
 * but renders Calibre-specific instructions instead of the web transfer UI.
 */
class CalibreConnectActivity final : public UiStatusActivity {
  CalibreConnectState state = CalibreConnectState::WIFI_SELECTION;

  std::unique_ptr<CrossPointWebServer> webServer;
  std::string connectedIP;
  std::string connectedSSID;
  unsigned long lastHandleClientTime = 0;
  size_t lastProgressReceived = 0;
  size_t lastProgressTotal = 0;
  std::string currentUploadName;
  std::string lastCompleteName;
  unsigned long lastCompleteAt = 0;
  unsigned long lastProcessedCompleteAt = 0;  // Track which server value we've already processed
  bool exitRequested = false;

  // Built when the server comes up (the address never changes while it runs) and
  // when a transfer moves, because both carry values no translation can hold.
  std::string ipLine;
  std::string transferLine;

  void onWifiSelectionComplete(bool connected);
  void startWebServer();
  void stopWebServer();

 public:
  explicit CalibreConnectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiStatusActivity("CalibreConnect", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  bool skipLoopDelay() override { return webServer && webServer->isRunning(); }
  bool preventAutoSleep() override { return webServer && webServer->isRunning(); }

 protected:
  StatusView statusView() const override;
  bool handleCustomInput() override;
};
