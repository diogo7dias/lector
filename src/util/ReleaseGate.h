#pragma once

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
// Pure state so it can be tested on the host: no HAL, no globals.
namespace input_gate {

class ReleaseGate {
 public:
  // Called when an activity is pushed, replaced or popped. Only a press that is
  // still held has a release to swallow, so an unheld arm is a no-op.
  void arm(const bool anyHeld) {
    if (anyHeld) armed_ = true;
  }

  // Once per input pass, after the buttons have been polled and before anything
  // queries them. The gate holds through the pass that carries the release edge,
  // which is the pass it exists to swallow, and opens on the next quiet pass.
  void tick(const bool anyHeld, const bool anyReleased) {
    if (!armed_) return;
    if (anyHeld || anyReleased) return;
    armed_ = false;
  }

  // True while releases must be reported to no one.
  bool swallowsRelease() const { return armed_; }

 private:
  bool armed_ = false;
};

}  // namespace input_gate
