#include <gtest/gtest.h>

#include "components/OptionPopupPaging.h"

using option_popup::pageFor;

namespace {
const std::vector<int> tenRows(10, 25);  // 4 fit in 100, so pages of 4, 4 and 2
}

TEST(OptionPopupPaging, EverythingFitsIsOnePage) {
  const auto page = pageFor(std::vector<int>(4, 25), 0, 100, 2);
  EXPECT_EQ(page.first, 0);
  EXPECT_EQ(page.count, 4);
  EXPECT_EQ(page.total, 1);
}

TEST(OptionPopupPaging, EverySelectionLandsOnThePageHoldingIt) {
  for (int selected = 0; selected < 10; ++selected) {
    const auto page = pageFor(tenRows, 0, 100, selected);
    EXPECT_LE(page.first, selected);
    EXPECT_LT(selected, page.first + page.count);
    EXPECT_EQ(page.number, selected / 4 + 1);
    EXPECT_EQ(page.total, 3);
  }
}

TEST(OptionPopupPaging, LastPageHoldsWhatIsLeft) {
  const auto page = pageFor(tenRows, 0, 100, 9);
  EXPECT_EQ(page.first, 8);
  EXPECT_EQ(page.count, 2);
}

TEST(OptionPopupPaging, SpacingCountsBetweenRowsOnly) {
  EXPECT_EQ(pageFor(std::vector<int>(4, 25), 5, 115, 0).count, 4);
  EXPECT_EQ(pageFor(std::vector<int>(4, 25), 5, 114, 0).count, 3);
}

TEST(OptionPopupPaging, ARowTallerThanTheListGetsItsOwnPage) {
  const auto page = pageFor({25, 300, 25}, 0, 100, 1);
  EXPECT_EQ(page.first, 1);
  EXPECT_EQ(page.count, 1);
  EXPECT_EQ(page.total, 3);
}
