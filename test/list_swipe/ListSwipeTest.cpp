#include <gtest/gtest.h>

#include "ListSwipeGesture.h"

namespace {

using list_swipe::Scroll;

// The X4 Pro reading surface: 480 x 800. Edge bands are the top and bottom 14%,
// so 112 px each.
constexpr int W = 480;
constexpr int H = 800;

Scroll swipe(int sx, int sy, int ex, int ey) { return list_swipe::scrollFrom(W, H, sx, sy, ex, ey); }

TEST(ListSwipe, SwipeUpInTheBodyScrollsDownThePage) {
  EXPECT_EQ(swipe(240, 600, 240, 300), Scroll::PageDown);
}

TEST(ListSwipe, SwipeDownInTheBodyScrollsUpThePage) {
  EXPECT_EQ(swipe(240, 300, 240, 600), Scroll::PageUp);
}

TEST(ListSwipe, ASwipeStartingInTheTopEdgeBandIsLeftToTheMenuGesture) {
  EXPECT_EQ(swipe(240, 40, 240, 400), Scroll::None);
}

TEST(ListSwipe, ASwipeStartingInTheBottomEdgeBandIsLeftToTheHomeGesture) {
  EXPECT_EQ(swipe(240, H - 40, 240, 300), Scroll::None);
}

TEST(ListSwipe, AMostlyHorizontalSwipeNeverScrolls) {
  EXPECT_EQ(swipe(100, 400, 400, 460), Scroll::None);
}

TEST(ListSwipe, AShortDragIsNotASwipe) {
  EXPECT_EQ(swipe(240, 400, 240, 380), Scroll::None);
}

TEST(ListSwipe, ADegenerateScreenNeverScrolls) {
  EXPECT_EQ(list_swipe::scrollFrom(0, 0, 0, 0, 0, 0), Scroll::None);
}

}  // namespace
