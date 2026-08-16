#pragma once

// One button that carries two actions: a short one and a hold one.
//
// The firmware used to decide between them on the release — hold past the threshold,
// let go, and only then did the hold action run. That reads as a dead button: the
// device knows what you asked for a second before it does anything about it.
//
// This fires the hold action the moment the threshold passes, while the button is
// still down, and then swallows the release so the short action does not follow it.
// Destructive hold actions are safe to fire early because they open a confirmation
// of their own; nothing here commits anything by itself.
//
// Host-testable: a small state machine over (held time, press edge, release edge).

#include <cstdint>

namespace hold_button {

enum class Fired : uint8_t {
  None,   // nothing to do this pass
  Hold,   // the threshold just passed, with the button still down
  Short,  // released before the threshold
};

class Tracker {
 public:
  // No hold action on this button in this context: then there is nothing to tell
  // apart, so the short action fires the instant the button goes down.
  Fired updatePressOnly(const bool wasPressedNow) { return wasPressedNow ? Fired::Short : Fired::None; }

  // `heldMs` is how long the button has been down (0 when it is not).
  // `isDown` / `wasReleasedNow` are this pass's level and release edge.
  Fired update(const bool isDown, const bool wasReleasedNow, const unsigned long heldMs,
               const unsigned long thresholdMs) {
    if (isDown) {
      // Exactly once per hold: `fired` is what stops a 3-second hold from firing on
      // every pass after the first.
      if (!fired && heldMs >= thresholdMs) {
        fired = true;
        return Fired::Hold;
      }
      return Fired::None;
    }

    if (wasReleasedNow) {
      const bool holdAlreadyRan = fired;
      fired = false;
      // The release that ends a hold is the tail of an action already taken, never a
      // short press of its own.
      return holdAlreadyRan ? Fired::None : Fired::Short;
    }

    return Fired::None;
  }

  // True between the hold firing and the button coming back up.
  bool holdFired() const { return fired; }
  void reset() { fired = false; }

 private:
  bool fired = false;
};

}  // namespace hold_button
