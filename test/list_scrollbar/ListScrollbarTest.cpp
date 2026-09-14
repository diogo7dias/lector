#include <gtest/gtest.h>

#include "components/ListScrollbar.h"

namespace {

// A list that fits on one screen has nothing to point at.
TEST(ListScrollbar, NoArrowsWhenEverythingFits) {
  const auto a = list_scrollbar::forWindow(10, 0, 10);
  EXPECT_FALSE(a.up);
  EXPECT_FALSE(a.down);
  const auto b = list_scrollbar::forWindow(4, 0, 10);
  EXPECT_FALSE(b.up);
  EXPECT_FALSE(b.down);
  const auto c = list_scrollbar::forWindow(0, 0, 10);
  EXPECT_FALSE(c.up);
  EXPECT_FALSE(c.down);
}

TEST(ListScrollbar, OnlyDownAtTheTop) {
  const auto a = list_scrollbar::forWindow(40, 0, 10);
  EXPECT_FALSE(a.up);
  EXPECT_TRUE(a.down);
}

TEST(ListScrollbar, OnlyUpAtTheBottom) {
  const auto a = list_scrollbar::forWindow(40, 30, 10);
  EXPECT_TRUE(a.up);
  EXPECT_FALSE(a.down);
}

TEST(ListScrollbar, BothInTheMiddle) {
  const auto a = list_scrollbar::forWindow(40, 15, 10);
  EXPECT_TRUE(a.up);
  EXPECT_TRUE(a.down);
}

// A window that ends exactly on the last row has nothing below it.
TEST(ListScrollbar, WindowEndingOnTheLastRowHasNoDown) {
  const auto a = list_scrollbar::forWindow(11, 1, 10);
  EXPECT_TRUE(a.up);
  EXPECT_FALSE(a.down);
}

// A negative start comes from callers that clamp elsewhere; it means the top.
TEST(ListScrollbar, NegativeStartMeansTheTop) {
  const auto a = list_scrollbar::forWindow(40, -3, 10);
  EXPECT_FALSE(a.up);
  EXPECT_TRUE(a.down);
}

TEST(ListScrollbar, VisibleRangeFormAgrees) {
  const auto top = list_scrollbar::forVisibleRange(40, 0, 9);
  EXPECT_FALSE(top.up);
  EXPECT_TRUE(top.down);
  const auto mid = list_scrollbar::forVisibleRange(40, 5, 12);
  EXPECT_TRUE(mid.up);
  EXPECT_TRUE(mid.down);
  const auto end = list_scrollbar::forVisibleRange(40, 33, 39);
  EXPECT_TRUE(end.up);
  EXPECT_FALSE(end.down);
  const auto all = list_scrollbar::forVisibleRange(3, 0, 2);
  EXPECT_FALSE(all.up);
  EXPECT_FALSE(all.down);
  const auto none = list_scrollbar::forVisibleRange(0, 0, 0);
  EXPECT_FALSE(none.up);
  EXPECT_FALSE(none.down);
}

}  // namespace
