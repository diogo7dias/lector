#pragma once

// Host-test stub for lib/Logging/Logging.h. The real header pulls in Arduino.h, which is where
// SdCardFont gets millis() and ESP; both are stubbed here with a heap large enough to never bind.

#include <cstdint>

namespace logging_stub {
inline void sink(const char*, ...) {}
}  // namespace logging_stub

#define LOG_ERR(origin, format, ...) logging_stub::sink(origin, format __VA_OPT__(, ) __VA_ARGS__)
#define LOG_INF(origin, format, ...) logging_stub::sink(origin, format __VA_OPT__(, ) __VA_ARGS__)
#define LOG_DBG(origin, format, ...) logging_stub::sink(origin, format __VA_OPT__(, ) __VA_ARGS__)

inline unsigned long millis() { return 0; }

struct EspStub {
  uint32_t getFreeHeap() const { return 1u << 20; }
  uint32_t getMaxAllocHeap() const { return 1u << 20; }
};
inline EspStub ESP;
