// Host tests for the Rotate button's three-way cycle.
//
// The stored values are not in visual order (Portrait 0, Landscape CW 1, Portrait
// Inverted 2, Landscape CCW 3), so the cycle cannot be arithmetic on the value. It is
// written out, which is also what keeps Portrait Inverted out of it: nothing offers that
// orientation and a button that reached it would strand the reader upside down.
#include <gtest/gtest.h>

#include "util/OrientationCycle.h"

using orientation_cycle::next;

namespace {
constexpr uint8_t PORTRAIT = 0;
constexpr uint8_t LANDSCAPE_CW = 1;
constexpr uint8_t PORTRAIT_INVERTED = 2;
constexpr uint8_t LANDSCAPE_CCW = 3;
}  // namespace

TEST(OrientationCycle, PortraitTurnsCounterClockwiseFirst) {
  // CCW is the panel's native orientation, so it costs no per-frame rotation.
  EXPECT_EQ(next(PORTRAIT), LANDSCAPE_CCW);
}

TEST(OrientationCycle, TheSecondPressTurnsTheOtherWay) {
  EXPECT_EQ(next(LANDSCAPE_CCW), LANDSCAPE_CW);
}

TEST(OrientationCycle, TheThirdPressIsBackToPortrait) {
  EXPECT_EQ(next(LANDSCAPE_CW), PORTRAIT);
}

TEST(OrientationCycle, ThreePressesReturnToWhereItStarted) {
  EXPECT_EQ(next(next(next(PORTRAIT))), PORTRAIT);
}

TEST(OrientationCycle, AnOrientationOutsideTheCycleLandsBackInIt) {
  // Portrait Inverted is reachable from the settings row but is not part of the cycle;
  // pressing Rotate there must not be a dead press.
  EXPECT_EQ(next(PORTRAIT_INVERTED), LANDSCAPE_CCW);
  EXPECT_EQ(next(99), LANDSCAPE_CCW);
}
