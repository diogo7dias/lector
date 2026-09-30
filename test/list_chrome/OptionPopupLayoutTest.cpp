#include <gtest/gtest.h>

#include "components/OptionPopupLayout.h"

namespace {

option_popup::Geometry withRows(const int rows, const int titleLines = 1) {
  option_popup::Geometry g;
  g.titleLines.assign(titleLines, "t");
  g.optionLines.assign(rows, std::vector<std::string>{"row"});
  return g;
}

}  // namespace

TEST(OptionPopupLayout, ACardSitsCentredWithItsRowsUnderTheHeading) {
  auto g = withRows(3);
  option_popup::place(g, 480, 800, 25);
  // 2 + 10 + 49 + 3 * 37 + 12 + 2 = 186 tall, centred.
  EXPECT_EQ(g.dialogH, 186);
  EXPECT_EQ(g.dialogY, (800 - 186) / 2);
  EXPECT_EQ(g.dialogX, 24);
  EXPECT_EQ(g.dialogW, 432);
  EXPECT_EQ(g.textX, 62);
  EXPECT_EQ(g.markerX, 40);
  EXPECT_EQ(g.ruleY, g.dialogY + 12 + 42);
  EXPECT_EQ(g.rowTop[0], g.dialogY + 12 + 49);
  EXPECT_EQ(g.rowTop[2], g.rowTop[0] + 74);
  EXPECT_EQ(option_popup::rowBaseline(g, 0), g.rowTop[0] + 27);
}

TEST(OptionPopupLayout, AWrappedTitleAndRowGrowByTheirLines) {
  auto g = withRows(2, 2);
  g.optionLines[1].push_back("more");
  option_popup::place(g, 480, 800, 25);
  EXPECT_EQ(g.ruleY, g.headTop + 42 + 33);
  EXPECT_EQ(g.rowTop[0], g.headTop + 49 + 33);
  EXPECT_EQ(g.rowHeight[1], 37 + 25);
}

TEST(OptionPopupLayout, ALongListSqueezesItsRowsButKeepsEveryOne) {
  // 28 actions (the hold binding picker) cannot fit at 37 a row on 800.
  auto g = withRows(28);
  option_popup::place(g, 480, 800, 25);
  EXPECT_EQ(g.rowTop.size(), 28u);
  EXPECT_LT(g.rowH, 37);
  EXPECT_GE(g.rowH, 25);
  EXPECT_LE(g.dialogH, 800);
  EXPECT_LE(g.rowTop[27] + g.rowHeight[27], g.dialogY + g.dialogH - 14);
}
