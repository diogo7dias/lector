#include <gtest/gtest.h>

#include "components/SettingsGrid.h"

namespace {

using settings_grid::cellAt;
using settings_grid::forPane;

constexpr int kPaneWidth = 480;
constexpr int kPaneTop = 200;
constexpr int kPaneHeight = 400;

settings_grid::Layout layoutFor(const int count, const int scrollRow = 0) {
  return forPane(kPaneWidth, kPaneHeight, count, scrollRow);
}

TEST(SettingsGrid, TwoCellsShareTheWidthWithOneGapBetweenThem) {
  const auto layout = layoutFor(22);
  const auto first = cellAt(layout, kPaneTop, 0);
  const auto second = cellAt(layout, kPaneTop, 1);
  EXPECT_EQ(first.x, settings_grid::kSidePad);
  EXPECT_EQ(first.width, second.width);
  EXPECT_EQ(second.x, first.x + first.width + settings_grid::kGap);
  EXPECT_LE(second.x + second.width, kPaneWidth - settings_grid::kSidePad);
  EXPECT_EQ(first.y, second.y);
}

TEST(SettingsGrid, CellsFillLeftToRightThenDown) {
  const auto layout = layoutFor(22);
  EXPECT_EQ(cellAt(layout, kPaneTop, 2).x, cellAt(layout, kPaneTop, 0).x);
  EXPECT_GT(cellAt(layout, kPaneTop, 2).y, cellAt(layout, kPaneTop, 0).y);
  EXPECT_EQ(cellAt(layout, kPaneTop, 3).x, cellAt(layout, kPaneTop, 1).x);
}

TEST(SettingsGrid, AnOddCountLeavesTheLastCellEmptyRatherThanStretchingOne) {
  const auto layout = layoutFor(21, /*scrollRow=*/10);
  EXPECT_EQ(layout.totalRows, 11);
  const auto last = cellAt(layout, kPaneTop, 20);
  EXPECT_EQ(last.x, settings_grid::kSidePad);
  EXPECT_EQ(last.width, cellAt(layout, kPaneTop, 19).width);
  // The cell beside it does not exist, and asking for it is not an error.
  EXPECT_EQ(cellAt(layout, kPaneTop, 21).width, 0);
}

TEST(SettingsGrid, TheGridHalvesTheScrollAgainstOneColumnOfRows) {
  // Twenty-two settings as full-width rows needed eleven rows' worth of scrolling more
  // than the grid does; this is the property the whole change exists for.
  const auto layout = layoutFor(22);
  EXPECT_EQ(layout.totalRows, 11);
  EXPECT_GE(layout.visibleRows, 5);
}

TEST(SettingsGrid, AGridThatFitsSpreadsOverThePaneInsteadOfLeavingDeadSpace) {
  const auto few = layoutFor(4);
  EXPECT_EQ(few.totalRows, 2);
  EXPECT_EQ(few.visibleRows, 2);
  EXPECT_GT(few.cellHeight, settings_grid::kMinCellHeight);
  EXPECT_LE(cellAt(few, kPaneTop, 3).y + few.cellHeight, kPaneTop + kPaneHeight);
}

TEST(SettingsGrid, EveryVisibleCellStaysInsideThePane) {
  const auto layout = layoutFor(22);
  for (int i = 0; i < 22; ++i) {
    const auto rect = cellAt(layout, kPaneTop, i);
    if (rect.width == 0) continue;
    EXPECT_GE(rect.y, kPaneTop);
    EXPECT_LE(rect.y + rect.height, kPaneTop + kPaneHeight);
  }
}

TEST(SettingsGrid, ScrolledCellsReportNoRect) {
  const auto layout = layoutFor(22, /*scrollRow=*/3);
  EXPECT_EQ(cellAt(layout, kPaneTop, 0).width, 0);
  EXPECT_GT(cellAt(layout, kPaneTop, 6).width, 0);
  EXPECT_EQ(cellAt(layout, kPaneTop, 6).y, kPaneTop);
}

TEST(SettingsGrid, ScrollingPastTheEndClampsToTheLastFullPane) {
  const auto layout = layoutFor(22, /*scrollRow=*/99);
  EXPECT_EQ(layout.scrollRow, layout.totalRows - layout.visibleRows);
}

TEST(SettingsGrid, ScrollToShowMovesAsLittleAsItCan) {
  const auto layout = layoutFor(22, /*scrollRow=*/4);
  // Already on screen: no movement at all.
  EXPECT_EQ(settings_grid::scrollToShow(layout, 8), 4);
  // Above the pane: the cell's own row becomes the top one.
  EXPECT_EQ(settings_grid::scrollToShow(layout, 0), 0);
  // Below it: the cell's row becomes the bottom one.
  EXPECT_EQ(settings_grid::scrollToShow(layout, 21), 10 - layout.visibleRows + 1);
}

TEST(SettingsGrid, UpAndDownMoveARowAndKeepTheColumn) {
  EXPECT_EQ(settings_grid::step(4, 22, /*deltaRows=*/1, /*deltaCells=*/0), 6);
  EXPECT_EQ(settings_grid::step(5, 22, /*deltaRows=*/-1, /*deltaCells=*/0), 3);
}

TEST(SettingsGrid, LeftAndRightMoveOneCell) {
  EXPECT_EQ(settings_grid::step(4, 22, 0, 1), 5);
  EXPECT_EQ(settings_grid::step(5, 22, 0, -1), 4);
}

TEST(SettingsGrid, AStepOffEitherEndStopsRatherThanWrapping) {
  EXPECT_EQ(settings_grid::step(0, 22, -1, 0), 0);
  EXPECT_EQ(settings_grid::step(0, 22, 0, -1), 0);
  EXPECT_EQ(settings_grid::step(21, 22, 1, 0), 21);
  EXPECT_EQ(settings_grid::step(21, 22, 0, 1), 21);
}

TEST(SettingsGrid, AnEmptyGridHasNothingToStepTo) {
  EXPECT_EQ(settings_grid::step(0, 0, 1, 0), 0);
  const auto layout = layoutFor(0);
  EXPECT_EQ(layout.totalRows, 0);
  EXPECT_EQ(cellAt(layout, kPaneTop, 0).width, 0);
}

}  // namespace
