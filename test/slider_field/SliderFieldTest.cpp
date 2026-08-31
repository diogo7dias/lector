#include <gtest/gtest.h>

#include "components/SliderField.h"

namespace {
constexpr slider_field::Range kPercent{0, 100};
constexpr slider_field::Range kMinutes{1, 60};
}  // namespace

TEST(SliderField, ClampsToTheEnds) {
  EXPECT_EQ(slider_field::clamp(-5, kPercent), 0);
  EXPECT_EQ(slider_field::clamp(140, kPercent), 100);
  EXPECT_EQ(slider_field::clamp(42, kPercent), 42);
}

TEST(SliderField, AnEmptyRangeCollapsesToItsFloor) {
  constexpr slider_field::Range single{7, 7};
  EXPECT_EQ(slider_field::clamp(3, single), 7);
  EXPECT_EQ(slider_field::valueForPermille(500, single), 7);
  EXPECT_EQ(slider_field::step(7, single, 1, /*wrap=*/true), 7);
}

TEST(SliderField, AnOrdinaryStepStopsAtTheEnds) {
  EXPECT_EQ(slider_field::step(58, kMinutes, 5, /*wrap=*/false), 60);
  EXPECT_EQ(slider_field::step(2, kMinutes, -5, /*wrap=*/false), 1);
  EXPECT_EQ(slider_field::step(30, kMinutes, 5, /*wrap=*/false), 35);
}

TEST(SliderField, AWrappingStepKeepsTheTopWhenItLandsThere) {
  // 90 + 10 is the end of the book, not the front cover.
  EXPECT_EQ(slider_field::step(90, kPercent, 10, /*wrap=*/true), 100);
  EXPECT_EQ(slider_field::step(99, kPercent, 1, /*wrap=*/true), 100);
}

TEST(SliderField, AWrappingStepComesOutTheOtherSide) {
  EXPECT_EQ(slider_field::step(95, kPercent, 10, /*wrap=*/true), 5);
  EXPECT_EQ(slider_field::step(0, kPercent, -1, /*wrap=*/true), 99);
  EXPECT_EQ(slider_field::step(100, kPercent, 1, /*wrap=*/true), 1);
}

TEST(SliderField, AWrappingStepRespectsAnOffsetFloor) {
  EXPECT_EQ(slider_field::step(60, kMinutes, 1, /*wrap=*/true), 2);
  EXPECT_EQ(slider_field::step(1, kMinutes, -1, /*wrap=*/true), 59);
}

TEST(SliderField, ADragLandsOnTheValueUnderTheFinger) {
  EXPECT_EQ(slider_field::valueForPermille(0, kPercent), 0);
  EXPECT_EQ(slider_field::valueForPermille(1000, kPercent), 100);
  EXPECT_EQ(slider_field::valueForPermille(500, kPercent), 50);
  // Rounds to the nearest value rather than truncating toward the floor.
  EXPECT_EQ(slider_field::valueForPermille(255, kPercent), 26);
}

TEST(SliderField, ADragOutsideTheTrackStaysInsideTheRange) {
  EXPECT_EQ(slider_field::valueForPermille(-40, kMinutes), 1);
  EXPECT_EQ(slider_field::valueForPermille(4000, kMinutes), 60);
}

TEST(SliderField, ADragOnAnOffsetRangeStartsAtItsFloor) {
  EXPECT_EQ(slider_field::valueForPermille(0, kMinutes), 1);
  EXPECT_EQ(slider_field::valueForPermille(1000, kMinutes), 60);
}

TEST(SliderField, TheX3SideButtonsAreFlipped) {
  // On the X4 the side buttons are an up/down rocker: up increases.
  const auto x4 = slider_field::sideDeltas(false, 10);
  EXPECT_EQ(x4.up, 10);
  EXPECT_EQ(x4.down, -10);
  // On the X3 they sit on the left and right edges, so the left one (which the
  // firmware calls Up) has to decrease.
  const auto x3 = slider_field::sideDeltas(true, 10);
  EXPECT_EQ(x3.up, -10);
  EXPECT_EQ(x3.down, 10);
}
