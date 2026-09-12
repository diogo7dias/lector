#include <gtest/gtest.h>

#include "ListSwipeGesture.h"
#include "util/ButtonNavigator.h"

namespace {

using list_swipe::Scroll;

// The X4 Pro reading surface: 480 x 800. Edge bands are the top and bottom 14%,
// so 112 px each.
constexpr int W = 480;
constexpr int H = 800;

Scroll swipe(int sx, int sy, int ex, int ey) { return list_swipe::scrollFrom(W, H, sx, sy, ex, ey); }

TEST(ListSwipe, SwipeUpInTheBodyScrollsDownThePage) { EXPECT_EQ(swipe(240, 600, 240, 300), Scroll::PageDown); }

TEST(ListSwipe, SwipeDownInTheBodyScrollsUpThePage) { EXPECT_EQ(swipe(240, 300, 240, 600), Scroll::PageUp); }

TEST(ListSwipe, ASwipeStartingInTheTopEdgeBandIsLeftToTheMenuGesture) {
  EXPECT_EQ(swipe(240, 40, 240, 400), Scroll::None);
}

TEST(ListSwipe, ASwipeStartingInTheBottomEdgeBandIsLeftToTheHomeGesture) {
  EXPECT_EQ(swipe(240, H - 40, 240, 300), Scroll::None);
}

TEST(ListSwipe, AMostlyHorizontalSwipeNeverScrolls) { EXPECT_EQ(swipe(100, 400, 400, 460), Scroll::None); }

TEST(ListSwipe, AShortDragIsNotASwipe) { EXPECT_EQ(swipe(240, 400, 240, 380), Scroll::None); }

TEST(ListSwipe, ADegenerateScreenNeverScrolls) { EXPECT_EQ(list_swipe::scrollFrom(0, 0, 0, 0, 0, 0), Scroll::None); }

}  // namespace

TEST(ListSwipe, OpdsReleaseAndContinuousDispatchOnlyOneOwner) {
  MappedInputManager input;
  ButtonNavigator::setMappedInputManager(input);
  ButtonNavigator nav;
  int rows = 0;
  int pages = 0;
  int selected = 9;
  const auto dispatch = [&] {
    // UiStatusActivity::navigateList(), used by OPDS: release then continuous.
    nav.onNextRelease([&] {
      ++rows;
      selected = ButtonNavigator::nextIndex(selected, 30);
    });
    nav.onPreviousRelease([&] {
      ++rows;
      selected = ButtonNavigator::previousIndex(selected, 30);
    });
    nav.onNextContinuous([&] {
      ++pages;
      selected = ButtonNavigator::nextPageIndex(selected, 30, 10);
    });
    nav.onPreviousContinuous([&] {
      ++pages;
      selected = ButtonNavigator::previousPageIndex(selected, 30, 10);
    });
  };
  input.scroll = Scroll::PageDown;
  dispatch();
  EXPECT_EQ(rows, 0);
  EXPECT_EQ(pages, 1);
  EXPECT_EQ(selected, 10);  // Previously row 10, then page 20.

  rows = pages = 0;
  input.scroll = Scroll::PageUp;
  dispatch();
  EXPECT_EQ(rows, 0);
  EXPECT_EQ(pages, 1);
  EXPECT_EQ(selected, 0);

  // A real release (also how hint-band controls arrive) still steps one row.
  rows = pages = 0;
  input.scroll = Scroll::None;
  input.released = true;
  dispatch();
  EXPECT_EQ(rows, 1);
  EXPECT_EQ(pages, 0);
  EXPECT_EQ(selected, 1);

  // Holding still repeats; its eventual release cannot add another row.
  input.released = false;
  input.held = true;
  nowMs = input.heldMs = 1000;
  dispatch();
  EXPECT_EQ(pages, 1);
  input.held = false;
  input.released = true;
  dispatch();
  EXPECT_EQ(rows, 1);
  EXPECT_EQ(pages, 1);
}
