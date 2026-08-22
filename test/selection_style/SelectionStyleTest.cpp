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

TEST(SelectionStyle, OnlySolidKnocksTheRowTextOutWhite) {
  EXPECT_TRUE(invertsText(SOLID));
  EXPECT_FALSE(invertsText(BRACKETS));
  EXPECT_FALSE(invertsText(CARET));
}

TEST(SelectionStyle, BracketsEmitTwoArmsPerCorner) {
  const auto painted = paint(BRACKETS, 10, 20, 300, 30);
  EXPECT_EQ(8u, painted.size());
  expectInsideRow(painted, 10, 20, 300, 30);
}

TEST(SelectionStyle, BracketsReachEveryCornerOfTheRow) {
  const auto painted = paint(BRACKETS, 10, 20, 300, 30);
  // Two arms start at the top-left corner; the other corners are reached by the
  // arms that end there, so check the corner each arm family anchors on.
  EXPECT_TRUE(hasBarAt(painted, 10, 20));  // top-left
  bool topRight = false, bottomLeft = false, bottomRight = false;
  for (const Bar& b : painted) {
    if (b.x + b.width == 310 && b.y == 20) topRight = true;
    if (b.x == 10 && b.y + b.height == 50) bottomLeft = true;
    if (b.x + b.width == 310 && b.y + b.height == 50) bottomRight = true;
  }
  EXPECT_TRUE(topRight);
  EXPECT_TRUE(bottomLeft);
  EXPECT_TRUE(bottomRight);
}

TEST(SelectionStyle, BracketArmsNeverMeetInTheMiddleOfANarrowRow) {
  // A 20px-wide row would have its four horizontal arms run into each other at
  // the default arm length, turning the brackets into a solid outline.
  const auto painted = paint(BRACKETS, 0, 0, 20, 10);
  expectInsideRow(painted, 0, 0, 20, 10);
  for (const Bar& b : painted) {
    EXPECT_LE(b.width, 10);
    EXPECT_LE(b.height, 5);
  }
}

TEST(SelectionStyle, CaretUnderlinesTheFullRowWidth) {
  const auto painted = paint(CARET, 10, 20, 300, 30);
  expectInsideRow(painted, 10, 20, 300, 30);
  bool underlined = false;
  for (const Bar& b : painted) {
    if (b.x == 10 && b.width == 300 && b.y + b.height == 50) underlined = true;
  }
  EXPECT_TRUE(underlined);
}

TEST(SelectionStyle, CaretPointsAtTheRowFromTheLeft) {
  const auto painted = paint(CARET, 10, 20, 300, 30);
  // Beyond the underline, the caret is a triangle built from columns that get
  // shorter towards its tip, so the marker reads as an arrow and not a block.
  std::vector<Bar> columns;
  for (const Bar& b : painted) {
    if (b.width != 300) columns.push_back(b);
  }
  ASSERT_GE(columns.size(), 3u);
  for (size_t i = 1; i < columns.size(); ++i) {
    EXPECT_LT(columns[i].height, columns[i - 1].height);
    EXPECT_GT(columns[i].x, columns[i - 1].x);
  }
}

TEST(SelectionStyle, EveryStyleFitsATinyRowWithoutEscapingIt) {
  for (const Style style : {SOLID, BRACKETS, CARET}) {
    const auto painted = paint(style, 3, 7, 12, 6);
    EXPECT_FALSE(painted.empty());
    expectInsideRow(painted, 3, 7, 12, 6);
  }
}

TEST(SelectionStyle, UnknownPersistedValueFallsBackToSolid) {
  // settings.json is user-editable and survives downgrades, so a value from a
  // firmware that knew more styles must not index past the enum.
  EXPECT_EQ(SOLID, fromSetting(0));
  EXPECT_EQ(BRACKETS, fromSetting(1));
  EXPECT_EQ(CARET, fromSetting(2));
  EXPECT_EQ(SOLID, fromSetting(3));
  EXPECT_EQ(SOLID, fromSetting(255));
}

TEST(SelectionStyle, CaretIsWideEnoughToReadOnEInk) {
  const auto painted = paint(CARET, 10, 20, 300, 30);
  int leftmost = 1 << 20, rightmost = 0;
  for (const Bar& b : painted) {
    if (b.width == 300) continue;  // the underline
    leftmost = b.x < leftmost ? b.x : leftmost;
    rightmost = (b.x + b.width) > rightmost ? (b.x + b.width) : rightmost;
  }
  // A one-pixel-per-column arrow all but vanishes at e-ink pitch.
  EXPECT_GE(rightmost - leftmost, 7);
}
