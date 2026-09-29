#include "WebServerSession.h"

#include <ESPmDNS.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <Logging.h>
#include <Memory.h>

#include "MappedInputManager.h"
#include "util/TaskWatchdog.h"

void restartMdns(const char* hostname, const char* tag) {
  MDNS.end();
  if (MDNS.begin(hostname)) {
    LOG_DBG(tag, "mDNS started: http://%s.local/", hostname);
  } else {
    LOG_DBG(tag, "WARNING: mDNS failed to start");
  }
}

std::unique_ptr<CrossPointWebServer> startServer(const GfxRenderer& renderer, MappedInputManager& input,
                                                 const char* tag) {
  // Heap-critical allocation: SD-font caches retained for the CJK UI fallback are
  // rebuildable. Release them right before the allocation: the WiFi selection
  // screen may have repopulated them rendering a CJK SSID.
  if (auto* fcm = renderer.getFontCacheManager()) {
    LOG_DBG(tag, "Free heap before SD font cache release: %d bytes", ESP.getFreeHeap());
    fcm->releaseSdFontCaches();
    LOG_DBG(tag, "Free heap before server alloc: %d bytes", ESP.getFreeHeap());
  }

  auto server = makeUniqueNoThrow<CrossPointWebServer>();
  if (!server) {
    LOG_ERR(tag, "OOM: CrossPointWebServer");
    return server;
  }
  // A URL fetch or an HTTP upload holds the loop for the length of the transfer,
  // so the server polls Back through here instead: without it the button is dead
  // until the transfer ends. Upload chunks can come ~0.5 s apart on a slow link, so
  // a button found down gets a second sample past the SDK's 5 ms debounce.
  server->setCancelPoll([&input] {
    input.update();
    if (gpio.rawInputActive()) {
      delay(6);
      input.update();
    }
    return input.isPressed(MappedInputManager::Button::Back) || input.wasPressed(MappedInputManager::Button::Back);
  });
  server->begin();
  return server;
}

bool pumpServer(CrossPointWebServer& server, MappedInputManager& input, unsigned long& lastHandleClientTime,
                const char* tag, const int budget, const int watchdogEvery, const int inputEvery) {
  const unsigned long timeSinceLastHandleClient = millis() - lastHandleClientTime;
  if (lastHandleClientTime > 0 && timeSinceLastHandleClient > 100) {
    LOG_DBG(tag, "WARNING: %lu ms gap since last handleClient", timeSinceLastHandleClient);
  }

  // Reset the watchdog before processing: HTTP header parsing can be slow.
  resetTaskWatchdogIfSubscribed();
  const int watchdogMask = watchdogEvery - 1;
  const int inputMask = inputEvery - 1;
  bool backPressed = false;
  for (int i = 0; i < budget && server.isRunning(); i++) {
    server.handleClient();
    if (server.takeUploadCancel()) {
      backPressed = true;
      break;
    }
    if ((i & watchdogMask) == watchdogMask) {
      resetTaskWatchdogIfSubscribed();
    }
    if ((i & inputMask) == inputMask) {
      yield();
      // Refresh the button state so Back is seen mid-pump, not only between frames.
      input.update();
      if (input.wasPressed(MappedInputManager::Button::Back)) {
        backPressed = true;
        break;
      }
    }
  }
  lastHandleClientTime = millis();
  return backPressed;
}
