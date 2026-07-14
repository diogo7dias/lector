#include "WakeFrameHandoff.h"

namespace wake_frame {

namespace {
bool s_armed = false;
uint32_t s_hash = 0;
}  // namespace

uint32_t hashBuffer(const uint8_t* data, size_t len) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < len; i++) {
    h ^= data[i];
    h *= 16777619u;
  }
  return h;
}

void arm(uint32_t restoredHash) {
  s_armed = true;
  s_hash = restoredHash;
}

bool isArmed() { return s_armed; }

void disarm() { s_armed = false; }

bool consumeIfMatch(const uint8_t* data, size_t len) {
  if (!s_armed) return false;
  s_armed = false;
  return hashBuffer(data, len) == s_hash;
}

}  // namespace wake_frame
