// Host tests for the selection-highlight geometry: which solid bars paint the
// focused row for each style, and whether the row's own text has to be knocked
// out white. The drawing itself is device-only.
#include <gtest/gtest.h>

#include <vector>

#include "SelectionStyle.h"

using namespace selection_style;

namespace {

std::vector<Bar> paint(const Style style, const int x, const int y, const int width, const int height) {
  Bar out[MAX_BARS];
  const int count = bars(style, x, y, width, height, out);
  return std::vector<Bar>(out, out + count);
}

// Every bar a style emits has to land inside the row it is painting, otherwise it
// bleeds into the neighbouring row on a tightly packed list.
void expectInsideRow(const std::vector<Bar>& painted, const int x, const int y, const int width, const int height) {
  for (const Bar& b : painted) {
    EXPECT_GE(b.x, x);
    EXPECT_GE(b.y, y);
    EXPECT_GT(b.width, 0);
    EXPECT_GT(b.height, 0);
    EXPECT_LE(b.x + b.width, x + width);
    EXPECT_LE(b.y + b.height, y + height);
  }
}

bool hasBarAt(const std::vector<Bar>& painted, const int x, const int y) {
  for (const Bar& b : painted) {
    if (b.x == x && b.y == y) return true;
  }
  return false;
}

}  // namespace

TEST(SelectionStyle, SolidFillsTheWholeRow) {
  const auto painted = paint(SOLID, 10, 20, 300, 30);
  ASSERT_EQ(1u, painted.size());
  EXPECT_EQ(10, painted[0].x);
  EXPECT_EQ(20, painted[0].y);
  EXPECT_EQ(300, painted[0].width);
  EXPECT_EQ(30, painted[0].height);
}

TEST(SelectionStyle, BothStylesKnockTheRowTextOutWhite) {
  // Both paint over the row, so the text on it has to be drawn white either way.
  EXPECT_TRUE(invertsText(SOLID));
  EXPECT_TRUE(invertsText(TIGHT));
}

TEST(SelectionStyle, TightLeavesPaperAtBothEndsOfTheRow) {
  const auto painted = paint(TIGHT, 10, 20, 300, 30);
  ASSERT_EQ(1u, painted.size());
  EXPECT_GT(painted[0].x, 10);
  EXPECT_LT(painted[0].x + painted[0].width, 310);
}

TEST(SelectionStyle, TightHugsTheRowsFirstLineWhenItHasOne) {
  Bar out[MAX_BARS];
  // A row two lines tall whose text starts at y=22 and runs 24 px per line.
  const int count = bars(TIGHT, 10, 20, 300, 60, out, /*firstLineY=*/22, /*firstLineHeight=*/24);
  ASSERT_EQ(1, count);
  EXPECT_LT(out[0].y, 22);
  EXPECT_GT(out[0].y + out[0].height, 22 + 24);
  EXPECT_LT(out[0].height, 60);
}

TEST(SelectionStyle, TightFallsBackToTheRowWithoutALine) {
  // A cover card or a tab measures no text line; the block then covers the row's
  // own band rather than nothing.
  const auto painted = paint(TIGHT, 10, 20, 300, 30);
  ASSERT_EQ(1u, painted.size());
  EXPECT_EQ(20, painted[0].y);
  EXPECT_EQ(30, painted[0].height);
}

TEST(SelectionStyle, EveryStyleFitsATinyRowWithoutEscapingIt) {
  for (const Style style : {SOLID, TIGHT}) {
    const auto painted = paint(style, 0, 0, 12, 6);
    expectInsideRow(painted, 0, 0, 12, 6);
  }
}

TEST(SelectionStyle, ANarrowRowStillGetsAMark) {
  // Trimming both ends off a row narrower than the inset would leave nothing to see.
  const auto painted = paint(TIGHT, 0, 0, 8, 20);
  ASSERT_EQ(1u, painted.size());
  EXPECT_GT(painted[0].width, 0);
}

TEST(SelectionStyle, UnknownPersistedValueFallsBackToSolid) {
  EXPECT_EQ(SOLID, fromSetting(9));
  EXPECT_EQ(SOLID, fromSetting(255));
  EXPECT_EQ(SOLID, fromSetting(0));
  EXPECT_EQ(TIGHT, fromSetting(1));
}

TEST(SelectionStyle, TheRetiredCaretValueReadsAsSolid) {
  // 0.27 dropped brackets (1) and the caret (2). Slot 1 is the new tight block, so
  // a settings.json still holding 2 lands on solid rather than past the enum.
  EXPECT_EQ(SOLID, fromSetting(2));
}

