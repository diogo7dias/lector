#include <gtest/gtest.h>

#include "src/util/TouchSleepPolicy.h"

namespace {

constexpr uint8_t kQuickLock = 35;
constexpr uint8_t kGoHome = 1;
constexpr uint8_t kDisabled = 0;

TEST(TouchSleepPolicy, DetectsHomeKeyQuickLockBindingOnAnyGesture) {
  EXPECT_FALSE(touch_sleep_policy::homeKeyUnlocksQuickLock(kGoHome, kDisabled, kDisabled, kQuickLock));
  EXPECT_TRUE(touch_sleep_policy::homeKeyUnlocksQuickLock(kQuickLock, kDisabled, kDisabled, kQuickLock));
  EXPECT_TRUE(touch_sleep_policy::homeKeyUnlocksQuickLock(kGoHome, kQuickLock, kDisabled, kQuickLock));
  EXPECT_TRUE(touch_sleep_policy::homeKeyUnlocksQuickLock(kGoHome, kDisabled, kQuickLock, kQuickLock));
}

TEST(TouchSleepPolicy, AllowsTouchSleepDuringQuickLockUnlessHomeKeyUnlocks) {
  // Disabled (0 ms) never sleeps touch.
  EXPECT_FALSE(touch_sleep_policy::shouldSleepTouch(/*quickLocked=*/false, false, 0, 60000));
  EXPECT_FALSE(touch_sleep_policy::shouldSleepTouch(/*quickLocked=*/true, false, 0, 60000));

  // Unlocked: sleeps once idle >= touchSleepMs.
  EXPECT_FALSE(touch_sleep_policy::shouldSleepTouch(false, true, 5000, 4999));
  EXPECT_TRUE(touch_sleep_policy::shouldSleepTouch(false, true, 5000, 5000));

  // Quick Locked without Home unlock: Touch Sleep IS allowed.
  EXPECT_FALSE(touch_sleep_policy::shouldSleepTouch(true, false, 5000, 4999));
  EXPECT_TRUE(touch_sleep_policy::shouldSleepTouch(true, false, 5000, 5000));

  // Quick Locked with Home unlock: Touch stays awake so Home key can unlock.
  EXPECT_FALSE(touch_sleep_policy::shouldSleepTouch(true, true, 5000, 60000));
}

}  // namespace
