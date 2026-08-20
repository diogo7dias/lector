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
// A release only counts when this tracker also saw the press that started it. Screens
// are swapped inside the pass that handled the press, so the screen that arrives sees
// nothing but the release a moment later; without that arming rule it would read that
// orphan release as a short press of its own and act on whatever its own selection
// happened to be.
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
  Fired updatePressOnly(const bool wasPressedNow) {
    // Still arms, so that a button which carries a hold action in one state and not in
    // another (Back, which only holds below the root folder) stays coherent when the
    // press lands on one path and the release on the other.
    if (wasPressedNow) armed = true;
    return wasPressedNow ? Fired::Short : Fired::None;
  }

  // `heldMs` is how long the button has been down (0 when it is not).
  // `wasPressedNow` / `isDown` / `wasReleasedNow` are this pass's press edge, level
  // and release edge.
  Fired update(const bool wasPressedNow, const bool isDown, const bool wasReleasedNow, const unsigned long heldMs,
               const unsigned long thresholdMs) {
    if (wasPressedNow) armed = true;

    if (isDown) {
      // Exactly once per hold: `fired` is what stops a 3-second hold from firing on
      // every pass after the first.
      if (armed && !fired && heldMs >= thresholdMs) {
        fired = true;
        return Fired::Hold;
      }
      return Fired::None;
    }

    if (wasReleasedNow) {
      const bool holdAlreadyRan = fired;
      const bool wasArmed = armed;
      fired = false;
      armed = false;
      // The release that ends a hold is the tail of an action already taken, never a
      // short press of its own. A release with no press behind it belongs to whatever
      // screen handled that press, so it is not this one's to act on either.
      return wasArmed && !holdAlreadyRan ? Fired::Short : Fired::None;
    }

    return Fired::None;
  }

  // True between the hold firing and the button coming back up.
  bool holdFired() const { return fired; }
  void reset() {
    fired = false;
    armed = false;
  }

 private:
  bool fired = false;
  bool armed = false;
};

}  // namespace hold_button
