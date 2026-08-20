#pragma once

// Pure policy: when does the low-battery warning fire, and when does it re-arm?
//
// The warning is a once-per-drain event, so the decision has to hold state across a chip
// reset and cannot simply be "is the charge low right now". Keeping the rule here, away
// from the main loop it is called from, is what makes it host-testable.
//
// Math core only: no SD, no settings object, no Arduino.

#include <cstdint>

namespace low_battery {

// Charge at or below this warns; charge above the clear level arms the warning again. The
// gap keeps a reading that hovers on the threshold from warning over and over.
inline constexpr uint16_t WARN_PERCENT = 10;
inline constexpr uint16_t CLEAR_PERCENT = 15;

enum class Action : uint8_t {
  None,        // nothing to do
  Warn,        // show the notice and remember that it was shown
  ClearLatch,  // the battery came back up: allow the next drain to warn again
};

struct Inputs {
  // The reading from the fuel gauge or ADC. Zero means "no usable reading": a failed I2C
  // read and a board with no battery backend both report it, and warning on that would
  // fire a "Battery low (0%)" at anyone whose gauge hiccups once.
  uint16_t percent = 0;
  // The notice has already been shown for this drain (persisted across sleep).
  bool alreadyWarned = false;
  // Plugged in, so the charge is on its way back up and a warning is noise.
  bool usbConnected = false;
  // The screen on top is one the notice may interrupt: the reader or home, never a
  // firmware update, a file transfer, the sleep screen or boot.
  bool screenAllowed = false;
};

inline constexpr Action resolve(const Inputs& in) {
  if (in.percent == 0) return Action::None;  // no usable reading

  // Re-arming outranks everything else, and deliberately ignores which screen is up: it
  // shows nothing, it only makes the next drain able to warn.
  if (in.alreadyWarned) return in.percent > CLEAR_PERCENT ? Action::ClearLatch : Action::None;

  if (in.percent > WARN_PERCENT) return Action::None;
  if (in.usbConnected || !in.screenAllowed) return Action::None;
  return Action::Warn;
}

}  // namespace low_battery
