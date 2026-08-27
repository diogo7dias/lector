// Host tests for the auto-sleep floor that catches a device with no way to sleep.
//
// Every gesture on every button can be rebound, Sleep included, so it is possible to
// leave a device with no manual route to sleep at all. The guard caps the auto-sleep
// timeout while that is true. It caps the value the loop reads and never the value
// stored, so binding Sleep again brings the user's own timeout straight back.
#include <gtest/gtest.h>

#include "util/SleepTimeoutGuard.h"

namespace {
constexpr uint8_t NEVER = 121;
}

TEST(SleepTimeoutGuard, WithSleepBoundTheStoredTimeoutIsUsedUntouched) {
  EXPECT_EQ(sleep_guard::effectiveMinutes(30, true, NEVER), 30);
  EXPECT_EQ(sleep_guard::effectiveMinutes(NEVER, true, NEVER), NEVER);
}

TEST(SleepTimeoutGuard, WithNoSleepBoundALongTimeoutIsCappedAtThreeMinutes) {
  EXPECT_EQ(sleep_guard::effectiveMinutes(30, false, NEVER), 3);
}

TEST(SleepTimeoutGuard, NeverIsCappedToo) {
  EXPECT_EQ(sleep_guard::effectiveMinutes(NEVER, false, NEVER), 3);
}

TEST(SleepTimeoutGuard, ATimeoutAlreadyShorterThanTheCapIsLeftAlone) {
  EXPECT_EQ(sleep_guard::effectiveMinutes(1, false, NEVER), 1);
  EXPECT_EQ(sleep_guard::effectiveMinutes(3, false, NEVER), 3);
}
