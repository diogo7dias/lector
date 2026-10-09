#pragma once

#include <cstdint>

// Swallows the release of a button that was already acted on by its press.
//
// Screens disagree on which edge acts: SettingsActivity opens a submenu on
// wasPressed(Confirm), UiListActivity activates a row on wasReleased(Confirm).
// One press of Confirm on "KOReader Sync" therefore opened the KOReader screen,
// and the finger coming off that same press landed as a row activation on the
// screen it had just opened -- the reader saw Settings jump straight into the
// Username keyboard and back out again, with no way to stay on the screen.
//
// Arming the gate when a screen changes makes the in-flight release reach
// nobody. It is the button counterpart of HalGPIO::suppressTouchContact().
//
// Per button: only the releases of the buttons held when the gate was armed are
// swallowed. A different key tapped while the first is still held is its own press,
// so its release still reaches the screen.
//
// Pure state so it can be tested on the host: no HAL, no globals. Masks carry one bit
// per physical button index.
namespace input_gate {

class ReleaseGate {
 public:
  // Called when an activity is pushed, replaced or popped. Only a press that is
  // still held has a release to swallow, so an unheld arm is a no-op.
  void arm(const uint8_t heldMask) { armed_ |= heldMask; }

  // Once per input pass, after the buttons have been polled and before anything
  // queries them. Each armed button holds through the pass that carries its release
  // edge, which is the pass it exists to swallow, and opens on its next quiet pass.
  void tick(const uint8_t heldMask, const uint8_t releasedMask) { armed_ &= (heldMask | releasedMask); }

  // True while this button's release must be reported to no one.
  bool swallowsRelease(const uint8_t button) const { return (armed_ >> button) & 1u; }
  // True while any release is owed, for events that belong to no one button (a gesture).
  bool armed() const { return armed_ != 0; }

 private:
  uint8_t armed_ = 0;
};

}  // namespace input_gate
