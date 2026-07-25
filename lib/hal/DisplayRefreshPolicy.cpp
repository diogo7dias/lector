#include "DisplayRefreshPolicy.h"

DisplayRefreshPolicy::Mode DisplayRefreshPolicy::choose(const Mode requested, uint32_t /*nowMs*/) {
  Mode chosen = requested;

  // Cap consecutive FAST refreshes so panel ghosting is still periodically
  // cleaned. We deliberately do NOT promote on idle time: on an e-reader "idle"
  // is the user reading the current page, which routinely exceeds a minute, so
  // an idle-triggered clean would fire on nearly every genuine page turn and
  // convert a fast async refresh into a slow, blocking one. Ghosting cleanup is
  // covered by this cap plus the reader's own every-N HALF cadence.
  if (requested == Mode::Fast && consecutiveFast_ >= MAX_CONSECUTIVE_FAST) {
    chosen = Mode::Clean;
  }

  if (chosen == Mode::Fast) {
    ++consecutiveFast_;
  } else {
    consecutiveFast_ = 0;
  }

  return chosen;
}

void DisplayRefreshPolicy::reset() { consecutiveFast_ = 0; }
