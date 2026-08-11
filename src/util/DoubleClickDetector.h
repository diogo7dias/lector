#pragma once

#include <cstdint>

namespace reader_input {

// Turns a stream of power-button release edges into single-click / double-click events.
//
// The cost of a double click is that a single click can no longer act on its own release:
// nothing can tell the two apart until the window has passed. So a release is held back for
// WINDOW_MS, and only then reported as Single. A second release inside the window reports
// Double and cancels the held-back Single.
//
// Deliberately free of millis(), settings and GPIO so it can be tested on the host. The
// caller owns the clock and decides when the detector is armed at all — see main.cpp, which
// only arms it while an EPUB is open, so the delay never reaches the home screen.
class DoubleClickDetector {
 public:
  // Long enough for a deliberate double click on a stiff side button, short enough to stay
  // inside the 500 ms a press has to act in. Not user-tunable on purpose.
  static constexpr uint32_t WINDOW_MS = 280;

  enum class Event : uint8_t {
    None,    // nothing to do this pass
    Single,  // the window closed with only one release in it
    Double,  // a second release arrived inside the window
  };

  // Call once per loop pass with this pass's release edge and the current millis().
  Event update(bool released, uint32_t nowMs) {
    if (released) {
      if (pending_) {
        pending_ = false;
        return Event::Double;
      }
      pending_ = true;
      firstReleaseMs_ = nowMs;
      return Event::None;
    }
    // Unsigned subtraction, so a millis() wrap does not strand a pending click.
    if (pending_ && nowMs - firstReleaseMs_ >= WINDOW_MS) {
      pending_ = false;
      return Event::Single;
    }
    return Event::None;
  }

  // True while a release is being held back. The caller uses this to hide the release edge
  // from everything downstream until the verdict is in.
  bool waiting() const { return pending_; }

  // Drop any held-back click. Called when the detector is disarmed (the book closed, the
  // setting changed), so a stale press cannot fire into a screen that never saw it.
  void reset() { pending_ = false; }

 private:
  bool pending_ = false;
  uint32_t firstReleaseMs_ = 0;
};

}  // namespace reader_input
