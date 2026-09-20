#pragma once

#include <cstdint>

// Two-tap confirmation for touch input.
//
// On a board with a touch panel a tap must not act on the spot. The first tap
// on a control ARMS it — it is highlighted and nothing else happens. A second
// tap on the SAME control runs the action. A tap on a DIFFERENT control moves
// the arm without acting, so exactly one control is ever armed.
//
// A control is named here by the (action, value) pair FreeInkUI dispatches for
// it, which is what keeps this a pure policy: no rects, no renderer, no screen,
// no allocation. One armed pair is the entire state, so the tap path costs a
// compare and two stores and never touches the heap.
//
// On a board without touch the gate is never enabled and decide() always
// answers Act, so a keys-only device runs exactly the code it ran before.
namespace two_tap {

// FreeInkUI's NO_ACTION. Spelled out rather than included so this header stays
// host-testable without the SDK.
inline constexpr uint16_t kNoAction = 0;

enum class Decision : uint8_t {
  Act,  // run the action now
  Arm,  // first tap: highlight this control and run nothing
};

class Gate {
 public:
  // True only on a board that actually has a touch panel. Everything below is
  // a pass-through until this is set.
  void setEnabled(const bool enabled) {
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    clear();
  }
  bool enabled() const { return enabled_; }

  // The whole state machine. `action` == kNoAction is not a control and never
  // arms.
  Decision decide(const uint16_t action, const int16_t value) {
    if (!enabled_ || action == kNoAction) return Decision::Act;
    if (armed_ && action == action_ && value == value_) {
      clear();
      return Decision::Act;
    }
    action_ = action;
    value_ = value;
    armed_ = true;
    return Decision::Arm;
  }

  // Clears the arm. Every lifetime rule — leaving the screen, scrolling the
  // list, a physical key, a tap on empty space — is this one call.
  void clear() {
    armed_ = false;
    action_ = kNoAction;
    value_ = 0;
  }

  bool armed() const { return armed_; }
  uint16_t armedAction() const { return action_; }
  int16_t armedValue() const { return value_; }

 private:
  bool enabled_ = false;
  bool armed_ = false;
  uint16_t action_ = kNoAction;
  int16_t value_ = 0;
};

// The list row a first tap left armed, for the screens that paint their own
// rows instead of going through FreeInkUI (home, the file browser, the
// end-of-book suggestions). Those draws already know each row's item index, so
// the arm reaches them as one number rather than as a second hit table. -1 =
// nothing armed, which is always the answer on a board without touch.
inline constexpr int kNoRow = -1;

inline int& armedRow() {
  static int row = kNoRow;
  return row;
}

}  // namespace two_tap
