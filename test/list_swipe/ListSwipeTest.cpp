#include <gtest/gtest.h>

#include <algorithm>

#include "ListSwipeGesture.h"
#include "util/ButtonNavigator.h"
#include "util/ListIndex.h"

namespace {

using list_swipe::Scroll;

// The X4 Pro reading surface: 480 x 800. The top band is 14% (112 px), the Home edge
// at the bottom 7% (56 px).
constexpr int W = 480;
constexpr int H = 800;

Scroll swipe(int sx, int sy, int ex, int ey) { return list_swipe::scrollFrom(W, H, sx, sy, ex, ey); }

TEST(ListSwipe, SwipeUpInTheBodyScrollsDownThePage) { EXPECT_EQ(swipe(240, 600, 240, 300), Scroll::PageDown); }

TEST(ListSwipe, SwipeDownInTheBodyScrollsUpThePage) { EXPECT_EQ(swipe(240, 300, 240, 600), Scroll::PageUp); }

TEST(ListSwipe, ASwipeStartingInTheTopEdgeBandIsLeftToTheMenuGesture) {
  EXPECT_EQ(swipe(240, 40, 240, 400), Scroll::None);
}

TEST(ListSwipe, ASwipeUpStartingInTheBottomEdgeBandIsLeftToTheHomeGesture) {
  EXPECT_EQ(swipe(240, H - 40, 240, 300), Scroll::None);
}

TEST(ListSwipe, ASwipeUpFromTheLastRowsAboveTheHomeEdgeScrolls) {
  EXPECT_EQ(swipe(240, H - 100, 240, 300), Scroll::PageDown);
}

TEST(ListSwipe, ASwipeDownStartingInTheHomeEdgeStillScrolls) {
  EXPECT_EQ(swipe(240, H - 50, 240, H - 2), Scroll::PageUp);
}

TEST(ListSwipe, AMostlyHorizontalSwipeNeverScrolls) { EXPECT_EQ(swipe(100, 400, 400, 460), Scroll::None); }

TEST(ListSwipe, AShortDragIsNotASwipe) { EXPECT_EQ(swipe(240, 400, 240, 380), Scroll::None); }

TEST(ListSwipe, ADegenerateScreenNeverScrolls) { EXPECT_EQ(list_swipe::scrollFrom(0, 0, 0, 0, 0, 0), Scroll::None); }

TEST(ListSwipe, BottomEdgeUpSwipeFollowsEdgeBandAndTravel) {
  using list_swipe::EDGE_BAND_FRAC_Y;
  using list_swipe::HOME_EDGE_FRAC_Y;
  using list_swipe::isBottomEdgeUpSwipe;

  // Outside books: Home band is the bottom 7% (56 px on 800 px height).
  EXPECT_TRUE(isBottomEdgeUpSwipe(H, 240, H - 40, 240, 300, HOME_EDGE_FRAC_Y));
  EXPECT_FALSE(isBottomEdgeUpSwipe(H, 240, H - 100, 240, 300, HOME_EDGE_FRAC_Y));

  // Inside books: reader-menu band is the bottom 14% (112 px on 800 px height).
  EXPECT_TRUE(isBottomEdgeUpSwipe(H, 240, H - 100, 240, 450, EDGE_BAND_FRAC_Y));
  EXPECT_FALSE(isBottomEdgeUpSwipe(H, 240, 600, 240, 300, EDGE_BAND_FRAC_Y));

  // Slow drag minTravelPx (20% = 160 px on 800 px height).
  EXPECT_TRUE(isBottomEdgeUpSwipe(H, 240, 720, 240, 560, EDGE_BAND_FRAC_Y, 160));
  EXPECT_FALSE(isBottomEdgeUpSwipe(H, 240, 720, 240, 600, EDGE_BAND_FRAC_Y, 160));

  // Downward, mostly horizontal, and degenerate dimensions are rejected.
  EXPECT_FALSE(isBottomEdgeUpSwipe(H, 240, 720, 240, 780, EDGE_BAND_FRAC_Y));
  EXPECT_FALSE(isBottomEdgeUpSwipe(H, 400, 720, 100, 700, EDGE_BAND_FRAC_Y));
  EXPECT_FALSE(isBottomEdgeUpSwipe(0, 240, 720, 240, 450, EDGE_BAND_FRAC_Y));

  // Landscape height (480 px -> 14% is 67 px).
  EXPECT_TRUE(isBottomEdgeUpSwipe(480, 400, 430, 400, 300, EDGE_BAND_FRAC_Y));
  EXPECT_FALSE(isBottomEdgeUpSwipe(480, 400, 380, 400, 200, EDGE_BAND_FRAC_Y));
}

}  // namespace

namespace {
// A list host as UiListActivity and UiStatusActivity wire it: onListNav with a window
// that a swipe pages through list_index::paged.
struct PagedList {
  MappedInputManager input;
  ButtonNavigator nav;
  int count = 30;
  int rowsOnScreen = 10;
  int top = 0;
  int selected = 0;
  int rowMoves = 0;
  int pageMoves = 0;

  PagedList() { ButtonNavigator::setMappedInputManager(input); }

  void dispatch() {
    nav.onListNav(
        selected, count,
        [&](int index) {
          ++rowMoves;
          selected = index;
          top = std::clamp(top, selected - rowsOnScreen + 1, selected);
        },
        [&](int direction) {
          ++pageMoves;
          const auto window = list_index::paged(top, selected, count, rowsOnScreen, direction);
          top = window.top;
          selected = window.selected;
        });
  }

  void swipe(Scroll scroll) {
    input.scroll = scroll;
    dispatch();
    input.scroll = Scroll::None;
  }
};
}  // namespace

TEST(ListSwipe, ASwipeMovesAWholeScreenAndClampsAtTheEnds) {
  PagedList list;  // 30 rows, 10 on screen
  for (const int expected : {10, 20, 20}) {
    list.swipe(Scroll::PageDown);
    EXPECT_EQ(list.top, expected);
    EXPECT_EQ(list.selected, expected) << "the selection travels with the window";
  }
  for (const int expected : {10, 0, 0}) {
    list.swipe(Scroll::PageUp);
    EXPECT_EQ(list.top, expected);
    EXPECT_EQ(list.selected, expected);
  }
  EXPECT_EQ(list.rowMoves, 0) << "a swipe never takes the row step";
}

TEST(ListSwipe, AButtonPressStillMovesOneRow) {
  PagedList list;
  list.swipe(Scroll::PageDown);
  list.input.released = true;
  list.dispatch();
  list.input.released = false;
  EXPECT_EQ(list.selected, 11);
  EXPECT_EQ(list.top, 10);
  EXPECT_EQ(list.rowMoves, 1);
  EXPECT_EQ(list.pageMoves, 1);
}

TEST(ListSwipe, AListThatFitsOnOneScreenDoesNotMove) {
  PagedList list;
  list.count = 6;
  list.selected = 2;
  list.swipe(Scroll::PageDown);
  list.swipe(Scroll::PageUp);
  EXPECT_EQ(list.top, 0);
  EXPECT_EQ(list.selected, 2);
}

TEST(ListSwipe, ReleaseAndContinuousDispatchOnlyOneOwner) {
  PagedList list;
  list.selected = 9;
  list.swipe(Scroll::PageDown);
  EXPECT_EQ(list.rowMoves, 0);
  EXPECT_EQ(list.pageMoves, 1);
  EXPECT_EQ(list.top, 10);

  // A real release (also how hint-band controls arrive) still steps one row.
  list.rowMoves = list.pageMoves = 0;
  const int before = list.selected;
  list.input.released = true;
  list.dispatch();
  EXPECT_EQ(list.rowMoves, 1);
  EXPECT_EQ(list.pageMoves, 0);
  EXPECT_EQ(list.selected, before + 1);

  // Holding still repeats; its eventual release cannot add another row.
  list.input.released = false;
  list.input.held = true;
  nowMs = list.input.heldMs = 1000;
  list.dispatch();
  EXPECT_EQ(list.rowMoves, 2);
  list.input.held = false;
  list.input.released = true;
  list.dispatch();
  EXPECT_EQ(list.rowMoves, 2);
  EXPECT_EQ(list.pageMoves, 0);
}
