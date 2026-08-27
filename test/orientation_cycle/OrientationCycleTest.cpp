// Host tests for the Rotate button's two-way cycle.
//
// The stored values are not in visual order (Portrait 0, Landscape CW 1, Portrait
// Inverted 2, Landscape CCW 3), so the cycle cannot be arithmetic on the value. It is
// written out, which is also what keeps the two orientations nobody asked for out of it:
// Portrait Inverted would strand the reader upside down, and the second landscape only
// made Rotate a three-press round trip back to the page you were reading.
#include <gtest/gtest.h>

#include "util/OrientationCycle.h"

using orientation_cycle::next;

namespace {
constexpr uint8_t PORTRAIT = 0;
constexpr uint8_t LANDSCAPE_CW = 1;
constexpr uint8_t PORTRAIT_INVERTED = 2;
constexpr uint8_t LANDSCAPE_CCW = 3;
}  // namespace

TEST(OrientationCycle, PortraitTurnsClockwise) { EXPECT_EQ(next(PORTRAIT), LANDSCAPE_CW); }

TEST(OrientationCycle, TheSecondPressIsBackToPortrait) { EXPECT_EQ(next(LANDSCAPE_CW), PORTRAIT); }

TEST(OrientationCycle, TwoPressesReturnToWhereItStarted) { EXPECT_EQ(next(next(PORTRAIT)), PORTRAIT); }

TEST(OrientationCycle, TheOtherLandscapeIsNoLongerPartOfTheCycle) {
  // Reachable from the settings row, never from the button.
  EXPECT_NE(next(PORTRAIT), LANDSCAPE_CCW);
  EXPECT_NE(next(LANDSCAPE_CW), LANDSCAPE_CCW);
}

TEST(OrientationCycle, AnOrientationOutsideTheCycleLandsBackInIt) {
  // Portrait Inverted and Landscape CCW are reachable from the settings row but are not
  // part of the cycle; pressing Rotate there must not be a dead press.
  EXPECT_EQ(next(PORTRAIT_INVERTED), PORTRAIT);
  EXPECT_EQ(next(LANDSCAPE_CCW), PORTRAIT);
  EXPECT_EQ(next(99), PORTRAIT);
}
