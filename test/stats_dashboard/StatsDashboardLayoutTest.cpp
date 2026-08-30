#include <gtest/gtest.h>

#include "components/StatsDashboardLayout.h"

namespace {

// Deliberately not divisible by three or seven, so every test here is also a
// test of where the rounding goes.
constexpr stats_dashboard::Rect kArea{10, 20, 460, 130};

}  // namespace

TEST(StatsMetricGrid, TheCellsTileTheAreaWithoutGapsOrOverlap) {
  int covered = 0;
  for (int i = 0; i < 6; ++i) {
    const auto cell = stats_dashboard::metricCell(kArea, i, 3, 2);
    covered += cell.width * cell.height;
    EXPECT_GE(cell.x, kArea.x);
    EXPECT_LE(cell.x + cell.width, kArea.x + kArea.width);
    EXPECT_GE(cell.y, kArea.y);
    EXPECT_LE(cell.y + cell.height, kArea.y + kArea.height);
  }
  EXPECT_EQ(covered, kArea.width * kArea.height);
}

TEST(StatsMetricGrid, TheLastColumnMeetsTheRightEdge) {
  const auto last = stats_dashboard::metricCell(kArea, 2, 3, 2);
  EXPECT_EQ(last.x + last.width, kArea.x + kArea.width);
}

TEST(StatsMetricGrid, TheLastRowMeetsTheBottomEdge) {
  const auto last = stats_dashboard::metricCell(kArea, 5, 3, 2);
  EXPECT_EQ(last.y + last.height, kArea.y + kArea.height);
}

TEST(StatsMetricGrid, CellsRunInReadingOrder) {
  const auto first = stats_dashboard::metricCell(kArea, 0, 3, 2);
  const auto second = stats_dashboard::metricCell(kArea, 1, 3, 2);
  const auto fourth = stats_dashboard::metricCell(kArea, 3, 3, 2);
  EXPECT_GT(second.x, first.x);
  EXPECT_EQ(second.y, first.y);
  EXPECT_EQ(fourth.x, first.x);
  EXPECT_GT(fourth.y, first.y);
}

TEST(StatsMetricGrid, AGridWithNoColumnsIsRefusedRatherThanDividingByZero) {
  const auto cell = stats_dashboard::metricCell(kArea, 0, 0, 2);
  EXPECT_EQ(cell.width, 0);
  EXPECT_EQ(cell.height, 0);
}

TEST(StatsBarChart, TheLabelAndTheTrackSplitTheRowAtTheLabelColumn) {
  const int rowHeight = stats_dashboard::barRowHeight(kArea.height, 4);
  const auto row = stats_dashboard::barRow(kArea, 0, 4, 80, rowHeight);
  EXPECT_EQ(row.label.width, 80);
  EXPECT_EQ(row.track.x, kArea.x + 80);
  EXPECT_EQ(row.label.x + row.label.width, row.track.x);
  EXPECT_EQ(row.track.x + row.track.width, kArea.x + kArea.width);
}

TEST(StatsBarChart, RowsStackWithoutOverlapping) {
  const int rowHeight = stats_dashboard::barRowHeight(kArea.height, 4);
  for (int i = 1; i < 4; ++i) {
    const auto previous = stats_dashboard::barRow(kArea, i - 1, 4, 80, rowHeight);
    const auto row = stats_dashboard::barRow(kArea, i, 4, 80, rowHeight);
    EXPECT_EQ(row.track.y, previous.track.y + previous.track.height);
  }
}

TEST(StatsBarChart, AShortAreaStillGivesARowWorthTouching) {
  EXPECT_GE(stats_dashboard::barRowHeight(/*areaHeight=*/4, 4), 8);
}

TEST(StatsColumnChart, TheColumnsAreCentredInTheirSlotsAndDoNotTouch) {
  for (int i = 0; i < 7; ++i) {
    const auto column = stats_dashboard::chartColumn(kArea, i, 7, 60);
    const auto slot = stats_dashboard::chartColumnSlot(kArea, i, 7);
    EXPECT_GE(column.x, slot.x);
    EXPECT_LE(column.x + column.width, slot.x + slot.width);
    // An odd slot cannot be split evenly, so the air differs by at most a pixel.
    EXPECT_NEAR(column.x - slot.x, slot.x + slot.width - (column.x + column.width), 1);
    if (i > 0) {
      const auto previous = stats_dashboard::chartColumn(kArea, i - 1, 7, 60);
      EXPECT_GT(column.x, previous.x + previous.width);
    }
  }
}

TEST(StatsColumnChart, ANarrowChartStillDrawsABar) {
  const auto column = stats_dashboard::chartColumn(stats_dashboard::Rect{0, 0, 14, 40}, 0, 7, 40);
  EXPECT_GE(column.width, 3);
}

TEST(StatsFill, NothingReadAtAllIsAnEmptyBar) {
  EXPECT_EQ(stats_dashboard::fillFor(100, 0, 500), 0);
  EXPECT_EQ(stats_dashboard::fillFor(100, 5, 0), 0);
}

TEST(StatsFill, TheLongestValueFillsTheWholeTrack) {
  EXPECT_EQ(stats_dashboard::fillFor(100, 500, 500), 100);
}

TEST(StatsFill, ATinyValueStillShows) {
  // Ten minutes against a hundred hours rounds to nothing, and a day that was
  // read on must not look like a day that was not.
  EXPECT_EQ(stats_dashboard::fillFor(100, 600, 360000), 1);
}
