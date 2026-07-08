#pragma once

#include <WebServer.h>

#include <memory>
#include <string>

#include "activities/Activity.h"

// Shares the currently-open book over WiFi: connects (or reuses a connection),
// starts a tiny one-file web server, and shows a QR code of its URL. Scanning it
// with a phone on the same network downloads the file. Mirrors CrossPointWebServer's
// lifecycle — WiFi is torn down with a silent restart on exit.
class QRShareActivity final : public Activity {
  enum class State { WIFI_SELECTION, QR_DISPLAY };
  State state = State::QR_DISPLAY;

  std::string filePath;
  std::string fileName;
  std::string serverUrl;
  std::unique_ptr<WebServer> server;
  bool serverRunning = false;

  void startServerAndShowQR();
  void onWifiSelectionComplete(bool connected);
  bool startServer();
  void stopServer();
  void handleDownload();

 public:
  QRShareActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return serverRunning; }
  bool preventAutoSleep() override { return serverRunning; }
};
