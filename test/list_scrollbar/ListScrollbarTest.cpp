#include <gtest/gtest.h>

#include "components/ListScrollbar.h"

namespace {

// A list that fits on one screen has nothing to indicate.
TEST(ListScrollbar, HidesItselfWhenEverythingFits) {
  EXPECT_FALSE(list_scrollbar::forList(0, 200, 10, 0, 10).visible);
  EXPECT_FALSE(list_scrollbar::forList(0, 200, 4, 0, 10).visible);
  EXPECT_FALSE(list_scrollbar::forList(0, 200, 0, 0, 10).visible);
}

TEST(ListScrollbar, ThumbCoversTheVisibleFraction) {
  // 10 rows visible of 40: a quarter of the track.
  const auto bar = list_scrollbar::forList(0, 200, 40, 0, 10);
  ASSERT_TRUE(bar.visible);
  EXPECT_EQ(bar.trackY, 0);
  EXPECT_EQ(bar.trackHeight, 200);
  EXPECT_EQ(bar.thumbHeight, 50);
  EXPECT_EQ(bar.thumbY, 0);
}

TEST(ListScrollbar, ThumbEndsFlushWithTheTrackOnTheLastPage) {
  // Last window starts at 30 of 40 items; the thumb must reach the bottom exactly, so
  // "am I at the end of the list" is answerable at a glance.
  const auto bar = list_scrollbar::forList(0, 200, 40, 30, 10);
  EXPECT_EQ(bar.thumbY + bar.thumbHeight, 200);
}

TEST(ListScrollbar, ThumbSitsProportionallyInBetween) {
  const auto bar = list_scrollbar::forList(0, 200, 40, 15, 10);
  // 15 of the 30 scrollable rows: halfway down the 150 px of travel.
  EXPECT_EQ(bar.thumbY, 75);
}

// A very long list would otherwise draw a thumb too small to see, or none at all.
TEST(ListScrollbar, ThumbNeverShrinksBelowTheMinimum) {
  const auto bar = list_scrollbar::forList(0, 200, 4000, 0, 10);
  EXPECT_EQ(bar.thumbHeight, list_scrollbar::kMinThumbHeight);
  const auto atEnd = list_scrollbar::forList(0, 200, 4000, 3990, 10);
  EXPECT_EQ(atEnd.thumbY + atEnd.thumbHeight, 200);
}

// Out-of-range window starts come from callers that clamp elsewhere; the bar must stay
// inside its track rather than draw off the end of the list.
TEST(ListScrollbar, ClampsAWindowStartOutOfRange) {
  const auto past = list_scrollbar::forList(0, 200, 40, 999, 10);
  EXPECT_EQ(past.thumbY + past.thumbHeight, 200);
  const auto before = list_scrollbar::forList(0, 200, 40, -5, 10);
  EXPECT_EQ(before.thumbY, 0);
}

TEST(ListScrollbar, TrackHugsTheRightEdgeOfTheList) {
  const auto bar = list_scrollbar::forList(0, 200, 40, 0, 10);
  EXPECT_EQ(list_scrollbar::trackX(0, 480), 480 - list_scrollbar::kRightMargin - list_scrollbar::kWidth);
  EXPECT_EQ(bar.thumbHeight > 0, true);
}

}  // namespace
