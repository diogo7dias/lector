#include <gtest/gtest.h>

#include "components/SettingsGrid.h"

// The grid navigation the settings screens share, exercised through the geometry
// the base drives it with: a pane that shrinks when a preview is reserved above
// it, and a selection that has to stay inside whatever is left.
namespace {

settings_grid::Layout paneOf(const int height, const int count, const int scrollRow = 0) {
  return settings_grid::forPane(300, height, count, scrollRow);
}

}  // namespace

TEST(GridNav, UpAndDownKeepTheColumn) {
  EXPECT_EQ(settings_grid::step(0, 8, 1, 0), 2);
  EXPECT_EQ(settings_grid::step(3, 8, 1, 0), 5);
  EXPECT_EQ(settings_grid::step(5, 8, -1, 0), 3);
}

TEST(GridNav, LeftAndRightMoveOneCell) {
  EXPECT_EQ(settings_grid::step(2, 8, 0, 1), 3);
  EXPECT_EQ(settings_grid::step(3, 8, 0, -1), 2);
}

TEST(GridNav, TheEndsClampRatherThanWrap) {
  EXPECT_EQ(settings_grid::step(0, 8, 0, -1), 0);
  EXPECT_EQ(settings_grid::step(7, 8, 0, 1), 7);
  EXPECT_EQ(settings_grid::step(7, 8, 1, 0), 7);
}

TEST(GridNav, AnEmptyGridStaysAtZero) {
  EXPECT_EQ(settings_grid::step(0, 0, 1, 0), 0);
  EXPECT_EQ(settings_grid::scrollToShow(paneOf(400, 0), 0), 0);
}

TEST(GridNav, AReservedPreviewLeavesFewerVisibleRows) {
  const auto full = paneOf(400, 20);
  const auto shrunk = paneOf(200, 20);
  EXPECT_GT(full.visibleRows, shrunk.visibleRows);
}

TEST(GridNav, TheViewportFollowsASelectionBelowIt) {
  const auto layout = paneOf(200, 20);
  const int scroll = settings_grid::scrollToShow(layout, 19);
  EXPECT_GT(scroll, 0);
  const auto scrolled = paneOf(200, 20, scroll);
  const auto rect = settings_grid::cellAt(scrolled, 100, 19);
  EXPECT_GT(rect.width, 0) << "the selected cell has to be on screen after the scroll";
}

TEST(GridNav, ShrinkingThePaneUnderTheSelectionScrollsToKeepIt) {
  // The preview grows when the font size does, so the grid loses rows under a
  // selection that was already near the bottom.
  const auto wide = paneOf(400, 20);
  const int wasVisible = settings_grid::scrollToShow(wide, 12);
  const auto narrow = paneOf(160, 20);
  const int nowVisible = settings_grid::scrollToShow(narrow, 12);
  EXPECT_GE(nowVisible, wasVisible);
  const auto scrolled = paneOf(160, 20, nowVisible);
  EXPECT_GT(settings_grid::cellAt(scrolled, 100, 12).width, 0);
}

TEST(GridNav, AnOddCountLeavesOneCellOnTheLastRow) {
  const auto layout = paneOf(400, 7);
  EXPECT_EQ(layout.totalRows, 4);
  EXPECT_GT(settings_grid::cellAt(layout, 0, 6).width, 0);
}

TEST(GridNav, TheHubAndACategoryScrollIndependently) {
  // Four hub cells always fit; a category of forty does not.
  EXPECT_EQ(settings_grid::scrollToShow(paneOf(400, 4), 3), 0);
  EXPECT_GT(settings_grid::scrollToShow(paneOf(400, 40), 39), 0);
}
