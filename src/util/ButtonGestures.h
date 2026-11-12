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
    const bool wasAwaiting = phase_ == Phase::AwaitingSecond;
    pressedAt_ = nowMs;
    phase_ = Phase::Down;
    if (!wasAwaiting) return Event::None;
    if (elapsed(nowMs, releasedAt_) < DOUBLE_WINDOW_MS) {
      phase_ = Phase::SecondDown;
      return hasDouble_ ? Event::Double : Event::None;
    }
    // The window closed while the loop was busy (an e-ink refresh), so no tick() ran to
    // report the first click. Report it now, or it is lost under this new press.
    return hasSingle_ ? Event::Single : Event::None;
  }

  Event onRelease(const uint32_t nowMs) {
    // The press already reported a gesture; its release is part of that gesture.
    const bool gestureReported = phase_ == Phase::SecondDown || phase_ == Phase::HoldFired;
    phase_ = Phase::Idle;
    releasedAt_ = nowMs;
    if (gestureReported) return Event::None;
    if (hasDouble_) {
      // Hold the single back: only the window's end tells the two apart.
      phase_ = Phase::AwaitingSecond;
      return Event::None;
    }
    return hasSingle_ ? Event::Single : Event::None;
  }

  // Called every loop pass, whether or not an edge arrived: hold fires while the
  // button is still down, and a deferred single fires when its window closes.
  Event tick(const uint32_t nowMs) {
    if ((phase_ == Phase::Down || phase_ == Phase::SecondDown) && hasHold_ && elapsed(nowMs, pressedAt_) >= holdMs_) {
      phase_ = Phase::HoldFired;
      return Event::Hold;
    }
    if (phase_ == Phase::AwaitingSecond && elapsed(nowMs, releasedAt_) >= DOUBLE_WINDOW_MS) {
      phase_ = Phase::Idle;
      return hasSingle_ ? Event::Single : Event::None;
    }
    return Event::None;
  }

  // True while the detector owes an answer: the caller must keep the button away
  // from the rest of the firmware until then, or the press would act twice.
  bool busy() const { return phase_ == Phase::AwaitingSecond || phase_ == Phase::HoldFired; }

  void reset() { phase_ = Phase::Idle; }

 private:
  // Unsigned subtraction, so a millis() wrap (every 49 days) reads as a small
  // elapsed time rather than as a window that can never close.
  static uint32_t elapsed(const uint32_t now, const uint32_t since) { return now - since; }

  bool hasSingle_ = true;
  bool hasDouble_ = false;
  bool hasHold_ = false;
  // SecondDown: the press that completed a double, so its release reports nothing.
  enum class Phase : uint8_t { Idle, Down, AwaitingSecond, SecondDown, HoldFired };
  Phase phase_ = Phase::Idle;
  uint32_t holdMs_ = HOLD_MS;
  uint32_t pressedAt_ = 0;
  uint32_t releasedAt_ = 0;
};

}  // namespace button_gestures
