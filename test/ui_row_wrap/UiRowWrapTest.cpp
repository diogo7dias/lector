#include <gtest/gtest.h>

#include "components/UiRowWrap.h"

namespace {

constexpr int kLine = 25;
constexpr int kPad = 5;
constexpr int kGap = 10;

// A wrap model: text `w` px wide takes ceil(w / width) lines.
std::function<int(int)> linesForWidth(const int textWidth) {
  return [textWidth](const int width) { return (textWidth + width - 1) / width; };
}

TEST(UiRowWrap, NameAndValueSharingOneLineKeepTheBaseRow) {
  const auto row = ui_row_wrap::forRow(200, 100, 440, kGap, kLine, kPad, linesForWidth(200), linesForWidth(100));
  EXPECT_FALSE(row.valueBelow);
  EXPECT_EQ(row.nameLines, 1);
  EXPECT_EQ(row.valueLines, 1);
  EXPECT_EQ(row.height, kLine + kPad);
}

TEST(UiRowWrap, ExactlyFillingTheLineStillShares) {
  const auto row = ui_row_wrap::forRow(300, 130, 440, kGap, kLine, kPad, linesForWidth(300), linesForWidth(130));
  EXPECT_FALSE(row.valueBelow);
  EXPECT_EQ(row.height, kLine + kPad);
}

TEST(UiRowWrap, OnePixelOverDropsTheValueUnderTheName) {
  const auto row = ui_row_wrap::forRow(301, 130, 440, kGap, kLine, kPad, linesForWidth(301), linesForWidth(130));
  EXPECT_TRUE(row.valueBelow);
  EXPECT_EQ(row.nameLines, 1);  // the name alone fits one line
  EXPECT_EQ(row.valueLines, 1);
  EXPECT_EQ(row.height, 2 * kLine + kPad);
}

TEST(UiRowWrap, ALongNameWrapsAndTheRowGrowsByItsLines) {
  // 900 px of name in 440: three lines, then the value under them.
  const auto row = ui_row_wrap::forRow(900, 60, 440, kGap, kLine, kPad, linesForWidth(900), linesForWidth(60));
  EXPECT_TRUE(row.valueBelow);
  EXPECT_EQ(row.nameLines, 3);
  EXPECT_EQ(row.valueLines, 1);
  EXPECT_EQ(row.height, 4 * kLine + kPad);
}

TEST(UiRowWrap, AValueWiderThanTheRowWrapsToo) {
  const auto row = ui_row_wrap::forRow(100, 500, 440, kGap, kLine, kPad, linesForWidth(100), linesForWidth(500));
  EXPECT_TRUE(row.valueBelow);
  EXPECT_EQ(row.nameLines, 1);
  EXPECT_EQ(row.valueLines, 2);
  EXPECT_EQ(row.height, 3 * kLine + kPad);
}

TEST(UiRowWrap, NoValueMeansOnlyTheNameCounts) {
  const auto fits = ui_row_wrap::forRow(200, 0, 440, kGap, kLine, kPad, linesForWidth(200), nullptr);
  EXPECT_FALSE(fits.valueBelow);
  EXPECT_EQ(fits.valueLines, 0);
  EXPECT_EQ(fits.height, kLine + kPad);
  const auto wraps = ui_row_wrap::forRow(500, 0, 440, kGap, kLine, kPad, linesForWidth(500), nullptr);
  EXPECT_EQ(wraps.nameLines, 2);
  EXPECT_EQ(wraps.height, 2 * kLine + kPad);
}

// A wrap model that reports zero lines must never produce an empty row.
TEST(UiRowWrap, NeverFewerThanOneLine) {
  const auto row = ui_row_wrap::forRow(900, 0, 440, kGap, kLine, kPad, [](int) { return 0; }, nullptr);
  EXPECT_EQ(row.nameLines, 1);
  EXPECT_EQ(row.height, kLine + kPad);
}

TEST(UiRowWrap, StackedCellIsBothBlocksPlusPadding) {
  EXPECT_EQ(ui_row_wrap::stackedHeight(1, 1, kLine, 8), 2 * kLine + 8);
  EXPECT_EQ(ui_row_wrap::stackedHeight(2, 1, kLine, 8), 3 * kLine + 8);
  EXPECT_EQ(ui_row_wrap::stackedHeight(0, 0, kLine, 8), kLine + 8);
}

}  // namespace
