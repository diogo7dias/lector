// Compile-and-behaviour check for the shared slider dialog.
//
// The dialog is a layout function over FreeInkUI, so the value the test can
// actually pin without a renderer is the arithmetic the callers hand it: the
// 0-based slider position and its span, and the permille a drag comes back as.
// Those are what break silently when a range has an offset floor (sleep timeout
// starts at 1 minute, not 0) — the readout stays right while the knob sits in
// the wrong place.
#include <gtest/gtest.h>

#include <algorithm>

namespace {

// The mapping every UiSliderDialogSpec caller fills in: value/max are relative
// to the range's floor, so a 1..60 range draws its knob at 0..59.
struct Position {
  int value;
  int max;
};

Position positionFor(const int value, const int minValue, const int maxValue) {
  return Position{value - minValue, std::max(1, maxValue - minValue)};
}

// What onSliderEvent does with a drag: permille of the track back to a value.
int valueForDrag(const int permille, const int minValue, const int maxValue) {
  const int range = std::max(1, maxValue - minValue);
  return minValue + (permille * range + 500) / 1000;
}

}  // namespace

TEST(UiSliderDialog, AZeroBasedRangeMapsStraightThrough) {
  const auto pos = positionFor(42, 0, 100);
  EXPECT_EQ(pos.value, 42);
  EXPECT_EQ(pos.max, 100);
}

TEST(UiSliderDialog, AnOffsetFloorPutsTheKnobAtTheStart) {
  // Sleep timeout runs 1..60: one minute is the far left, not one sixtieth in.
  const auto low = positionFor(1, 1, 60);
  EXPECT_EQ(low.value, 0);
  EXPECT_EQ(low.max, 59);
  const auto high = positionFor(60, 1, 60);
  EXPECT_EQ(high.value, 59);
}

TEST(UiSliderDialog, ACollapsedRangeNeverDividesByZero) {
  const auto pos = positionFor(7, 7, 7);
  EXPECT_EQ(pos.value, 0);
  EXPECT_EQ(pos.max, 1);
}

TEST(UiSliderDialog, ADragLandsOnTheValueUnderTheFinger) {
  EXPECT_EQ(valueForDrag(0, 0, 100), 0);
  EXPECT_EQ(valueForDrag(1000, 0, 100), 100);
  EXPECT_EQ(valueForDrag(500, 0, 100), 50);
}

TEST(UiSliderDialog, ADragOnAnOffsetRangeStartsAtItsFloor) {
  EXPECT_EQ(valueForDrag(0, 1, 60), 1);
  EXPECT_EQ(valueForDrag(1000, 1, 60), 60);
  // Rounds to the nearest value rather than truncating toward the floor.
  EXPECT_EQ(valueForDrag(500, 1, 60), 31);
}
