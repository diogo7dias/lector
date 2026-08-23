#include <gtest/gtest.h>

#include "util/HoldRepeat.h"

// The ramp is the whole point: a held button has to stay fine-grained long enough to be
// usable as a nudge, then go coarse before the wide ranges (line spacing spans 35..150)
// turn into a fourteen-second hold.

TEST(HoldRepeat, TheFirstRepeatsStepOneAtATime) {
  EXPECT_EQ(holdRepeatStep(0), 1);
  EXPECT_EQ(holdRepeatStep(1), 1);
  EXPECT_EQ(holdRepeatStep(HOLD_REPEAT_COARSE_AFTER - 1), 1);
}

TEST(HoldRepeat, LaterRepeatsStepFive) {
  EXPECT_EQ(holdRepeatStep(HOLD_REPEAT_COARSE_AFTER), 5);
  EXPECT_EQ(holdRepeatStep(HOLD_REPEAT_COARSE_AFTER + 1), 5);
  EXPECT_EQ(holdRepeatStep(10000), 5);
}

TEST(HoldRepeat, TheRampNeverSkipsBackToFine) {
  int previous = 0;
  for (unsigned i = 0; i < 100; ++i) {
    const int step = holdRepeatStep(i);
    EXPECT_GE(step, previous);
    previous = step;
  }
}

TEST(HoldRepeat, GoesCoarseAboutOneSecondIntoTheHold) {
  // 8 fine repeats at the 120 ms edit interval is 960 ms of holding before the
  // ramp; a value under 5 would feel twitchy, one over 12 makes the hold drag.
  EXPECT_GE(HOLD_REPEAT_COARSE_AFTER, 5u);
  EXPECT_LE(HOLD_REPEAT_COARSE_AFTER, 12u);
}
