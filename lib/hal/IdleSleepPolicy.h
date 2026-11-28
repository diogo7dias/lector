#pragma once
#include <cstdint>

namespace idle_sleep {

// X4 Pro light-sleep slice ceilings:
// - Touch awake: 40 ms so taps (~80-150 ms) and swipes (~200-400 ms) are never
//   missed even if a GT911 unit pulses INT briefly or runs in QEMU (where
//   GT911 does not drive GPIO10). On hardware with level INT, touch wakes early.
// - Touch asleep: 250 ms because only buttons (GPIO0/7/3) can wake, all armed
//   for level GPIO wakeup.
inline constexpr uint32_t SLICE_TOUCH_AWAKE_MS = 40;
inline constexpr uint32_t SLICE_TOUCH_ASLEEP_MS = 250;

// X3/X4 (C3) idle raw-input poll interval: raised from 10 ms (100 Hz) to 35 ms
// (~28.5 Hz), leaving margin for 5 ms debounce inside an 80 ms double-click.
inline constexpr uint32_t C3_IDLE_POLL_MS = 35;

struct Conditions {
  bool wifiActive = false;
  bool usbConnected = false;
  bool usbSerialJtagActive = false;
  bool renderIdle = false;
  bool powerLockHeld = false;
  bool rawInputActive = false;
  bool touchHeld = false;
  bool touchAsleep = false;
  bool gpioWakeArmed = false;
};

constexpr bool canLightSleep(const Conditions& c) {
  return !c.wifiActive && !c.usbConnected && !c.usbSerialJtagActive && c.renderIdle && !c.powerLockHeld &&
         !c.rawInputActive && !c.touchHeld && c.gpioWakeArmed;
}

constexpr uint32_t sliceDurationMs(const bool touchAsleep, const uint32_t maxUntilDeadlineMs) {
  const uint32_t base = touchAsleep ? SLICE_TOUCH_ASLEEP_MS : SLICE_TOUCH_AWAKE_MS;
  return maxUntilDeadlineMs < base ? maxUntilDeadlineMs : base;
}

// Arm the opposite of the idle GPIO10 level when GT911 is awake with no touch
// down so light sleep cannot immediately self-wake on the idle line level.
constexpr bool touchWakeHighLevel(const int idleIntLevel) { return idleIntLevel == 0; }

}  // namespace idle_sleep
