#pragma once

#include <functional>
#include <memory>
#include <string>

#include "NetworkModeSelectionActivity.h"
#include "activities/UiStatusActivity.h"
#include "network/CrossPointWebServer.h"

// Web server activity states
enum class WebServerActivityState {
  MODE_SELECTION,  // Choosing between Join Network and Create Hotspot
  WIFI_SELECTION,  // WiFi selection subactivity is active (for Join Network mode)
  AP_STARTING,     // Starting Access Point mode
  SERVER_RUNNING,  // Web server is running and handling requests
  SHUTTING_DOWN    // Shutting down server and WiFi
};

/**
 * CrossPointWebServerActivity is the entry point for file transfer functionality.
 * It:
 * - First presents a choice between "Join a Network" (STA), "Connect to Calibre", and "Create Hotspot" (AP)
 * - For STA mode: Launches WifiSelectionActivity to connect to an existing network
 * - For AP mode: Creates an Access Point that clients can connect to
 * - Starts the CrossPointWebServer when connected
 * - Handles client requests in its loop() function
 * - Cleans up the server and shuts down WiFi on exit
 */
class CrossPointWebServerActivity final : public UiStatusActivity {
  WebServerActivityState state = WebServerActivityState::MODE_SELECTION;

  // Network mode
  NetworkMode networkMode = NetworkMode::JOIN_NETWORK;
  bool isApMode = false;
  // Join Network brought the radio up in this session. Switching it off again does not
  // undo the heap fragmentation (SilentRestart.h), so the way out still reboots.
  bool wifiUsed = false;
  int modeRow = 0;  // the mode list's row on entry

  // Web server - owned by this activity
  std::unique_ptr<CrossPointWebServer> webServer;

  // Server status
  std::string connectedIP;
  std::string connectedSSID;  // For STA mode: network name, For AP mode: AP name

  // Performance monitoring
  unsigned long lastHandleClientTime = 0;

  // Sustained WiFi-loss tracking; abandon only after WIFI_ABANDON_MS.
  int consecutiveDisconnects = 0;
  unsigned long lastWifiCheck = 0;  // per instance: a function static carried over to the next session
  unsigned long firstDisconnectAt = 0;
  static constexpr unsigned long WIFI_ABANDON_MS = 5UL * 60UL * 1000UL;

  // Cached signal-strength bracket (0..4) for the WiFi indicator.
  int lastWifiBars = 0;

  // Everything the running screen shows that carries a value: built when the
  // server comes up, because none of it changes while it runs.
  std::string wifiQrPayload;   // the WIFI: config a phone joins the hotspot with
  std::string urlQrPayload;    // the address the code opens
  std::string hostnameLine;    // that address, written out
  std::string ipFallbackLine;  // and the raw IP, for a phone that cannot resolve it

  void onNetworkModeSelected(NetworkMode mode);
  void onWifiSelectionComplete(bool connected);
  void startAccessPoint();
  void startWebServer();
  // Records why the screen closed, then leaves: the exit reboots, so the
  // diagnostics file is the only place the cause survives.
  void closeTransfer(const char* cause);

 public:
  // modeRow: the mode list's row on entry (the mode a Back from its screen came from).
  explicit CrossPointWebServerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int modeRow = 0)
      : UiStatusActivity(activity_name::kCrossPointWebServer, renderer, mappedInput), modeRow(modeRow) {}
  void onEnter() override;
  void onExit() override;
  // Rebuilt on its mode after a restart from a screen over it (Calibre).
  bool restartStep(uint8_t& opener, int& row) const override;
  bool skipLoopDelay() override { return webServer && webServer->recentlyActive(); }
  bool preventAutoSleep() override { return webServer && webServer->recentlyActive(); }

 protected:
  StatusView statusView() const override;
  bool handleCustomInput() override;
};
