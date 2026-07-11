#include "DisplayRefreshPolicy.h"

DisplayRefreshPolicy::Mode DisplayRefreshPolicy::choose(const Mode requested, const uint32_t nowMs) {
  const bool idle = hasRefreshed_ && static_cast<uint32_t>(nowMs - lastRefreshMs_) >= IDLE_CLEANUP_MS;
  Mode chosen = requested;

  if (requested == Mode::Fast && (idle || consecutiveFast_ >= MAX_CONSECUTIVE_FAST)) {
    chosen = Mode::Clean;
  }

  if (chosen == Mode::Fast) {
    ++consecutiveFast_;
  } else {
    consecutiveFast_ = 0;
  }

  lastRefreshMs_ = nowMs;
  hasRefreshed_ = true;
  return chosen;
}

void DisplayRefreshPolicy::reset() {
  lastRefreshMs_ = 0;
  consecutiveFast_ = 0;
  hasRefreshed_ = false;
}
