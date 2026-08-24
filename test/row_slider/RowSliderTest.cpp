#include <gtest/gtest.h>

#include "components/RowSlider.h"

namespace {

// A settings row as the list paints it on the X4 Pro: full width, one line tall.
constexpr int kRowX = 0;
constexpr int kRowY = 300;
constexpr int kRowWidth = 480;
constexpr int kRowHeight = 30;

row_slider::Bar barForRow() { return row_slider::barFor(kRowX, kRowY, kRowWidth, kRowHeight); }

// The armed row keeps its label on the left and its number on the right; the track
// takes the gap between them.
constexpr int kLabelWidth = 160;
constexpr int kValueWidth = 60;

TEST(RowSlider, TheBarSitsInsideTheRowWithAirOnBothSides) {
  const row_slider::Bar bar = barForRow();
  EXPECT_GT(bar.x, kRowX);
  EXPECT_LT(bar.x + bar.width, kRowX + kRowWidth);
  EXPECT_GE(bar.y, kRowY);
  EXPECT_LE(bar.y + bar.height, kRowY + kRowHeight);
}

TEST(RowSlider, TheBarEndsAreTheEndsOfTheRange) {
  const row_slider::Bar bar = barForRow();
  EXPECT_EQ(row_slider::valueForX(bar, bar.x, 0, 100, 1), 0);
  EXPECT_EQ(row_slider::valueForX(bar, bar.x + bar.width, 0, 100, 1), 100);
}

TEST(RowSlider, ATouchPastEitherEndClampsToTheRange) {
  const row_slider::Bar bar = barForRow();
  EXPECT_EQ(row_slider::valueForX(bar, bar.x - 500, 35, 150, 1), 35);
  EXPECT_EQ(row_slider::valueForX(bar, bar.x + bar.width + 500, 35, 150, 1), 150);
}

TEST(RowSlider, AMiddleTouchLandsInTheMiddleOfTheRange) {
  const row_slider::Bar bar = barForRow();
  EXPECT_EQ(row_slider::valueForX(bar, bar.x + bar.width / 2, 0, 100, 1), 50);
}

TEST(RowSlider, DraggingSnapsToTheSettingsOwnStep) {
  // A margin declares a step of 5, so a drag lands on multiples of 5 and never
  // between them.
  const row_slider::Bar bar = barForRow();
  for (int x = bar.x; x <= bar.x + bar.width; ++x) {
    EXPECT_EQ(row_slider::valueForX(bar, x, 0, 100, 5) % 5, 0);
  }
}

TEST(RowSlider, ASnappedDragStillReachesAnEndThatIsNotOnTheStep) {
  // Line spacing runs 35 to 150 in fives: 150 is 23 steps up from 35, but a range
  // whose span is not a whole number of steps must still reach its own maximum.
  const row_slider::Bar bar = barForRow();
  EXPECT_EQ(row_slider::valueForX(bar, bar.x + bar.width, 35, 148, 5), 148);
}

TEST(RowSlider, TheFilledWidthFollowsTheValue) {
  const row_slider::Bar bar = barForRow();
  EXPECT_EQ(row_slider::filledWidth(bar, 0, 0, 100), 0);
  EXPECT_EQ(row_slider::filledWidth(bar, 100, 0, 100), bar.width);
  EXPECT_EQ(row_slider::filledWidth(bar, 50, 0, 100), bar.width / 2);
}

TEST(RowSlider, TheTrackTakesTheGapBetweenLabelAndValue) {
  const row_slider::Bar bar =
      row_slider::barBetween(kRowX + kLabelWidth, kRowX + kRowWidth - kValueWidth, kRowY, kRowHeight);
  EXPECT_GE(bar.x, kRowX + kLabelWidth);
  EXPECT_LE(bar.x + bar.width, kRowX + kRowWidth - kValueWidth);
  EXPECT_GT(bar.width, 0);
}

TEST(RowSlider, ALabelThatEatsTheRowLeavesNoTrack) {
  // A long translated label plus its number can fill the row; the caller then draws
  // the plain number rather than a track too narrow to aim at.
  const row_slider::Bar bar = row_slider::barBetween(kRowX + 400, kRowX + 430, kRowY, kRowHeight);
  EXPECT_EQ(bar.width, 0);
}

TEST(RowSlider, ARowTooShortForABarReportsNoBar) {
  // Nothing to drag, so the caller draws the plain number instead of a sliver.
  const row_slider::Bar bar = row_slider::barFor(0, 0, 480, 2);
  EXPECT_EQ(bar.width, 0);
}

TEST(RowSlider, AnEmptyRangeIsNotDividedBy) {
  const row_slider::Bar bar = barForRow();
  EXPECT_EQ(row_slider::valueForX(bar, bar.x + 10, 7, 7, 1), 7);
  EXPECT_EQ(row_slider::filledWidth(bar, 7, 7, 7), 0);
}

}  // namespace
