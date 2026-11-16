#pragma once
#include <HalStorage.h>

#include <cstdint>

inline void vTaskDelay(uint32_t) {}

// The converters' free-heap gates always pass: the host has room for any decoder.
struct EspStub {
  size_t getFreeHeap() const { return 256 * 1024; }
};
inline EspStub ESP;
