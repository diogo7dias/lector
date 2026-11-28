#pragma once

#include <cstdint>

namespace touch_sleep_policy {

// True when any gesture on the Home key is bound to Quick Lock in the active
// context. On boards where the capacitive Home key is reported by the touch
// controller (GT911), sleeping touch while Quick Lock is active would prevent
// the user from unlocking via that Home key binding.
inline constexpr bool homeKeyUnlocksQuickLock(const uint8_t singleBinding, const uint8_t doubleBinding,
                                              const uint8_t holdBinding, const uint8_t quickLockAction) {
  return singleBinding == quickLockAction || doubleBinding == quickLockAction || holdBinding == quickLockAction;
}

// Decide whether to put the touch controller into sleep mode.
// Touch Sleep is allowed during Quick Lock unless the Home key is needed to
// unlock Quick Lock in the current context.
inline constexpr bool shouldSleepTouch(const bool quickLocked, const bool homeUnlocksQuickLock,
                                       const uint32_t touchSleepMs, const uint32_t idleMs) {
  if (touchSleepMs == 0) return false;
  if (quickLocked && homeUnlocksQuickLock) return false;
  return idleMs >= touchSleepMs;
}

}  // namespace touch_sleep_policy
