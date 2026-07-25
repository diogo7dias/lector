#pragma once

#include <cstdint>

class DisplayRefreshPolicy {
 public:
  enum class Mode : uint8_t { Fast, Clean, Full };

  static constexpr uint8_t MAX_CONSECUTIVE_FAST = 12;

  // nowMs is accepted for call-site stability but no longer consulted: idle time
  // on an e-reader is the user reading the page, so it must never trigger a
  // clean. Ghosting cleanup is driven by MAX_CONSECUTIVE_FAST instead.
  Mode choose(Mode requested, uint32_t nowMs);
  void reset();

 private:
  uint8_t consecutiveFast_ = 0;
};
