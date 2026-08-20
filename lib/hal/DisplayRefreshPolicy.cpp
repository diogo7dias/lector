#include "DisplayRefreshPolicy.h"

DisplayRefreshPolicy::Mode DisplayRefreshPolicy::choose(const Mode requested, uint32_t /*nowMs*/,
                                                        const uint16_t inkScore) {
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

  // What the counters cannot see: how much ink THIS frame is about to move. The frame
  // about to be pushed is included in the total before the comparison, because it is the
  // pass that would leave the panel over the line — cleaning after it has already run
  // means the reader sees the ghosting first and the fix second.
  //
  // Only ever escalates. A frame scored at zero leaves both branches above exactly as
  // they were, which is what makes this safe to add underneath a policy that already
  // works.
  if (requested == Mode::Fast && chosen == Mode::Fast) {
    const uint32_t projected = static_cast<uint32_t>(inkDebt_) + inkScore;
    if (projected >= DEBT_FULL_THRESHOLD) {
      chosen = Mode::Full;
    } else if (projected >= DEBT_CLEAN_THRESHOLD) {
      chosen = Mode::Clean;
    }
  }

  if (chosen == Mode::Fast) {
    ++consecutiveFast_;
    ++fastSinceFull_;
    addDebt(inkScore);
  } else {
    consecutiveFast_ = 0;
    if (chosen == Mode::Full) {
      fastSinceFull_ = 0;
      // A full waveform discharges the panel: nothing is left owing.
      inkDebt_ = 0;
    } else {
      // A Clean scrubs but does not discharge, so most of the debt goes and the rest
      // stays. The residue is what eventually earns a FULL on a screen that keeps
      // demanding cleans.
      inkDebt_ /= CLEAN_DISCHARGE_DIVISOR;
    }
  }

  return chosen;
}

void DisplayRefreshPolicy::reset() {
  consecutiveFast_ = 0;
  fastSinceFull_ = 0;
  inkDebt_ = 0;
}
