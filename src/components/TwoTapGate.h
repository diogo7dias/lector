#pragma once

#include <cstdint>

// Confirming tap for touch input.
//
// A tap acts on the spot, except on the few controls whose action cannot be
// undone (delete, clear, flash, forget; UiAppHost::needsConfirmTap names them).
// The first tap on one of those ARMS it: it is outlined and nothing else
// happens. A second tap on the SAME control runs the action. A tap on a
// DIFFERENT control moves the arm without acting, so exactly one control is
// ever armed.
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

}  // namespace two_tap
