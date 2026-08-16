#pragma once

// When a newly opened screen is allowed to start reading buttons.
//
// Buttons now act on the press rather than on the release, which means the press
// that opened a screen is very often STILL HELD when that screen appears. Without a
// gate the same physical push would fire on both screens: open the menu and
// immediately activate whatever row the cursor happens to sit on.
//
// So every screen change arms this gate, and the new screen sees nothing until the
// user has let go of everything. Releasing costs nothing — the press already did its
// work on the screen that is now gone — and a press that starts after the screen is
// up is unaffected, so the instant feel is kept exactly where it matters.
//
// Host-testable: a two-state machine over "is anything held right now".

#include <cstdint>

namespace input_arming {

class Gate {
 public:
  // A screen change happened: ignore input until everything is released.
  void arm() { waiting = true; }

  // Call once per input update with the current held state. Returns true when the
  // screen may read button edges this pass.
  bool update(const bool anythingHeld) {
    // Disarms on the first pass with nothing held, and that same pass is already
    // live: nothing can have been pressed in it, so there is no edge to lose.
    if (waiting && !anythingHeld) waiting = false;
    return !waiting;
  }

  bool armed() const { return waiting; }

 private:
  // Starts false: the very first screen after boot has no earlier press to inherit.
  bool waiting = false;
};

}  // namespace input_arming
