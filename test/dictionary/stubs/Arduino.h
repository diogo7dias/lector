#pragma once

#include <cstddef>

struct EspStub {
  size_t maxAllocHeap = 1024 * 1024;
  size_t getMaxAllocHeap() const { return maxAllocHeap; }
};
inline EspStub ESP;
inline unsigned long millis() { return 0; }
