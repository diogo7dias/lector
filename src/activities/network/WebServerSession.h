#pragma once

#include <memory>

#include "network/CrossPointWebServer.h"

class GfxRenderer;
class MappedInputManager;

// The server lifecycle shared by the file-transfer and Calibre screens.

// Stops and restarts mDNS under `hostname`.
void restartMdns(const char* hostname, const char* tag);

// Releases the SD-font caches, allocates the server and begins it, with Back
// polled during a URL fetch (the fetch holds the loop for the whole transfer).
// nullptr on OOM; otherwise check isRunning().
std::unique_ptr<CrossPointWebServer> startServer(GfxRenderer& renderer, MappedInputManager& input, const char* tag);

// Up to `budget` handleClient() calls, feeding the watchdog every `watchdogEvery`
// and yielding to poll Back every `inputEvery` (both powers of two). True when
// Back stopped the pump.
bool pumpServer(CrossPointWebServer& server, MappedInputManager& input, unsigned long& lastHandleClientTime,
                const char* tag, int budget, int watchdogEvery, int inputEvery);
