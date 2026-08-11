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

  // A Clean pass resets consecutiveFast_ but does not discharge the panel, so on
  // its own the cap above lets ghosting build without bound. Escalate to a real
  // FULL once enough FAST passes have gone by since the last one.
  if (requested == Mode::Fast && fastSinceFull_ >= MAX_FAST_BEFORE_FULL) {
    chosen = Mode::Full;
  }

  if (chosen == Mode::Fast) {
    ++consecutiveFast_;
    ++fastSinceFull_;
  } else {
    consecutiveFast_ = 0;
    if (chosen == Mode::Full) {
      fastSinceFull_ = 0;
    }
  }

  return chosen;
}

void DisplayRefreshPolicy::reset() {
  consecutiveFast_ = 0;
  fastSinceFull_ = 0;
}
