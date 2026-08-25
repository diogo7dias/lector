#pragma once

#include <cstdint>

// Turns one button's press/release edges into single-click, double-click and hold.
//
// Pure state, no hardware and no clock of its own: the caller feeds it edges and the
// current millis(), which is what makes the timing testable on the host.
//
// The two waits are armed only where the user actually bound something, and that is
// the whole design. Waiting for a second press delays the first one, so a button with
// nothing on its double click reports Single the moment it comes up — a page turn
// never pays for a gesture nobody asked for. Same for hold: with nothing bound the
// detector stays quiet and the firmware's own hold behaviour (page repeat, list
// paging) keeps the press.
namespace button_gestures {

// A second press this soon after the first is the same gesture. Long enough for a
// deliberate double tap on a stiff side key, short enough not to read as a pause.
constexpr uint32_t DOUBLE_WINDOW_MS = 280;
// Matches the hold the reader menu and the settings rows already use, so a hold feels
// the same length wherever it is bound.
constexpr uint32_t HOLD_MS = 500;

enum class Event : uint8_t { None, Single, Double, Hold };

class Detector {
 public:
  // hasDouble/hasHold: whether the user bound anything to those gestures on this
  // button in the CURRENT context. Called every time the context changes (entering a
  // book, leaving it), because the same key can carry a double in one and not the
  // other.
  void configure(const bool hasDouble, const bool hasHold) {
    hasDouble_ = hasDouble;
    hasHold_ = hasHold;
  }
  // A button with no single-click binding still reports Single by default: the caller
  // decides whether to act on it. Clearing this silences the button entirely.
  void setSingleBound(const bool bound) { hasSingle_ = bound; }
  // How long this button's hold is. Defaults to HOLD_MS; the power button keeps the
  // user's own sleepHoldMs, because that is the threshold its hold has always used.
  void setHoldMs(const uint32_t ms) { holdMs_ = ms; }

  Event onPress(const uint32_t nowMs) {
    holdFired_ = false;
    pressedAt_ = nowMs;
    pressed_ = true;
    if (awaitingSecond_ && elapsed(nowMs, releasedAt_) < DOUBLE_WINDOW_MS) {
      awaitingSecond_ = false;
      swallowRelease_ = true;
      return hasDouble_ ? Event::Double : Event::None;
    }
    awaitingSecond_ = false;
    return Event::None;
  }

  Event onRelease(const uint32_t nowMs) {
    pressed_ = false;
    releasedAt_ = nowMs;
    if (swallowRelease_ || holdFired_) {
      // The press already reported a gesture; its release is part of that gesture.
      swallowRelease_ = false;
      holdFired_ = false;
      return Event::None;
    }
    if (!hasSingle_ && !hasDouble_) return Event::None;
    if (hasDouble_) {
      // Hold the single back: only the window's end tells the two apart.
      awaitingSecond_ = true;
      return Event::None;
    }
    return hasSingle_ ? Event::Single : Event::None;
  }

  // Called every loop pass, whether or not an edge arrived: hold fires while the
  // button is still down, and a deferred single fires when its window closes.
  Event tick(const uint32_t nowMs) {
    if (pressed_ && hasHold_ && !holdFired_ && elapsed(nowMs, pressedAt_) >= holdMs_) {
      holdFired_ = true;
      return Event::Hold;
    }
    if (awaitingSecond_ && elapsed(nowMs, releasedAt_) >= DOUBLE_WINDOW_MS) {
      awaitingSecond_ = false;
      return hasSingle_ ? Event::Single : Event::None;
    }
    return Event::None;
  }

  // True while the detector owes an answer: the caller must keep the button away
  // from the rest of the firmware until then, or the press would act twice.
  bool busy() const { return awaitingSecond_ || holdFired_; }

  void reset() {
    pressed_ = false;
    awaitingSecond_ = false;
    holdFired_ = false;
    swallowRelease_ = false;
  }

 private:
  // Unsigned subtraction, so a millis() wrap (every 49 days) reads as a small
  // elapsed time rather than as a window that can never close.
  static uint32_t elapsed(const uint32_t now, const uint32_t since) { return now - since; }

  bool hasSingle_ = true;
  bool hasDouble_ = false;
  bool hasHold_ = false;
  bool pressed_ = false;
  bool awaitingSecond_ = false;
  bool holdFired_ = false;
  bool swallowRelease_ = false;
  uint32_t holdMs_ = HOLD_MS;
  uint32_t pressedAt_ = 0;
  uint32_t releasedAt_ = 0;
};

}  // namespace button_gestures
