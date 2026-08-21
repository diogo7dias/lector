#include <gtest/gtest.h>

#include <vector>

#include "components/WrappedListWindow.h"

namespace {

// A list of same-height rows, the ordinary case: uniform single-line titles.
std::function<int(int)> uniform(const int height) {
  return [height](int) { return height; };
}

// heightOf for a list where specific rows wrap over extra lines.
std::function<int(int)> varying(std::vector<int> heights) {
  return [heights = std::move(heights)](const int index) {
    return index >= 0 && index < static_cast<int>(heights.size()) ? heights[index] : 20;
  };
}

constexpr int kGap = 2;

}  // namespace

TEST(WrappedListWindow, EmptyListDrawsNothing) {
  const auto win = wrapped_list::window(0, 0, 0, 500, kGap, uniform(20));
  EXPECT_EQ(win.count, 0);
}

// The bug this file exists for. A 500px list of 20px rows fits 22 rows once the 2px gaps
// are counted; before the fix a hard-coded cap of 14 rows drew fourteen and left the rest
// of the screen blank.
TEST(WrappedListWindow, FillsTheAvailableHeightRatherThanAFixedRowCount) {
  const auto win = wrapped_list::window(3000, 0, 0, 500, kGap, uniform(20));
  EXPECT_EQ(win.first, 0);
  EXPECT_EQ(win.count, 22);
}

TEST(WrappedListWindow, StopsAtTheEndOfAShortList) {
  const auto win = wrapped_list::window(5, 0, 0, 500, kGap, uniform(20));
  EXPECT_EQ(win.count, 5);
}

TEST(WrappedListWindow, KeepsTheOffsetWhenTheSelectionIsAlreadyVisible) {
  const auto win = wrapped_list::window(3000, 105, 100, 500, kGap, uniform(20));
  EXPECT_EQ(win.first, 100);
  EXPECT_GT(win.count, 5);
}

TEST(WrappedListWindow, SelectionAboveTheWindowBecomesTheTopRow) {
  const auto win = wrapped_list::window(3000, 40, 100, 500, kGap, uniform(20));
  EXPECT_EQ(win.first, 40);
}

// The second half of the reported bug: scrolling one past the last visible row used to
// leave a window that did not contain the selection at all, so nothing was highlighted.
TEST(WrappedListWindow, SelectionBelowTheWindowLandsOnTheBottomRow) {
  const auto win = wrapped_list::window(3000, 22, 0, 500, kGap, uniform(20));
  EXPECT_EQ(win.first + win.count - 1, 22);
  EXPECT_EQ(win.count, 22);
}

TEST(WrappedListWindow, SelectionIsAlwaysInsideTheWindowWalkingTheWholeList) {
  constexpr int kItems = 200;
  int offset = 0;
  for (int selected = 0; selected < kItems; selected++) {
    const auto win = wrapped_list::window(kItems, selected, offset, 500, kGap, uniform(20));
    EXPECT_GE(selected, win.first) << "selection above window at " << selected;
    EXPECT_LE(selected, win.first + win.count - 1) << "selection below window at " << selected;
    offset = win.first;
  }
  // And back up again, which exercises the "selection above the window" branch.
  for (int selected = kItems - 1; selected >= 0; selected--) {
    const auto win = wrapped_list::window(kItems, selected, offset, 500, kGap, uniform(20));
    EXPECT_GE(selected, win.first) << "selection above window at " << selected;
    EXPECT_LE(selected, win.first + win.count - 1) << "selection below window at " << selected;
    offset = win.first;
  }
}

// Rows are not uniform in a wrapped list: a long filename spills over several lines. The
// window has to fit by height, not by count.
TEST(WrappedListWindow, TallRowsReduceHowManyFit) {
  const auto win = wrapped_list::window(10, 0, 0, 200, kGap, varying({80, 80, 80, 20, 20, 20, 20, 20, 20, 20}));
  EXPECT_EQ(win.first, 0);
  EXPECT_EQ(win.count, 2);  // 80 + 2 + 80 = 162 fits, a third 80 would not
}

TEST(WrappedListWindow, SelectionBelowAWindowOfMixedHeightsStaysVisible) {
  const auto heights = varying({20, 20, 20, 100, 100, 20, 20, 20, 20, 20});
  const auto win = wrapped_list::window(10, 8, 0, 200, kGap, heights);
  EXPECT_GE(8, win.first);
  EXPECT_LE(8, win.first + win.count - 1);
}

// A single title so long it wraps past the whole list height still has to be drawn, or the
// screen goes blank on the row the user is standing on.
TEST(WrappedListWindow, RowTallerThanTheListIsStillDrawn) {
  const auto win = wrapped_list::window(5, 2, 2, 100, kGap, uniform(400));
  EXPECT_EQ(win.first, 2);
  EXPECT_EQ(win.count, 1);
}

TEST(WrappedListWindow, StaleOffsetPastTheEndIsClamped) {
  const auto win = wrapped_list::window(10, 0, 9999, 500, kGap, uniform(20));
  EXPECT_EQ(win.first, 0);  // selection at 0 pulls the window back to the top
  EXPECT_EQ(win.count, 10);
}

TEST(WrappedListWindow, NegativeOffsetIsClamped) {
  const auto win = wrapped_list::window(10, -1, -5, 500, kGap, uniform(20));
  EXPECT_EQ(win.first, 0);
}

// EpubReaderMenuActivity draws lists with no cursor at all; the window then just honours
// whatever offset it was given.
TEST(WrappedListWindow, NoSelectionKeepsTheOffset) {
  const auto win = wrapped_list::window(3000, -1, 100, 500, kGap, uniform(20));
  EXPECT_EQ(win.first, 100);
}
