#pragma once

#include <cstdint>

class DisplayRefreshPolicy {
 public:
  enum class Mode : uint8_t { Fast, Clean, Full };

  static constexpr uint32_t IDLE_CLEANUP_MS = 60000;
  static constexpr uint8_t MAX_CONSECUTIVE_FAST = 12;

  Mode choose(Mode requested, uint32_t nowMs);
  void reset();

 private:
  uint32_t lastRefreshMs_ = 0;
  uint8_t consecutiveFast_ = 0;
  bool hasRefreshed_ = false;
};
