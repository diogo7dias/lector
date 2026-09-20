#include <gtest/gtest.h>

#include "ListSwipeGesture.h"
#include "activities/reader/ReaderTouchZones.h"

namespace {

using reader_touch::MenuMode;
using reader_touch::Mode;
using reader_touch::TapAction;

// The X4 Pro reading surface: 480 px wide, 800 tall.
constexpr int W = 480;
constexpr int H = 800;

TapAction tap(Mode mode, MenuMode menu, int x, int y) { return reader_touch::tapAction(mode, menu, W, H, x, y); }

TEST(ReaderTouch, TapModeTurnsPagesFromTheOuterThirds) {
  EXPECT_EQ(tap(Mode::Tap, MenuMode::Tap, 10, 400), TapAction::Prev);
  EXPECT_EQ(tap(Mode::Tap, MenuMode::Tap, W - 10, 400), TapAction::Next);
}

TEST(ReaderTouch, InvertedTapSwapsTheTwoSides) {
  EXPECT_EQ(tap(Mode::InvertedTap, MenuMode::Tap, 10, 400), TapAction::Next);
  EXPECT_EQ(tap(Mode::InvertedTap, MenuMode::Tap, W - 10, 400), TapAction::Prev);
}

TEST(ReaderTouch, TheCentreThirdOfBothAxesOpensTheMenu) {
  EXPECT_EQ(tap(Mode::Tap, MenuMode::Tap, W / 2, H / 2), TapAction::Menu);
}

TEST(ReaderTouch, TheCentreColumnOutsideTheMiddleBandDoesNothing) {
  EXPECT_EQ(tap(Mode::Tap, MenuMode::Tap, W / 2, 20), TapAction::None);
  EXPECT_EQ(tap(Mode::Tap, MenuMode::Tap, W / 2, H - 20), TapAction::None);
}

TEST(ReaderTouch, SwipeModeLeavesEveryTapToTheMenu) {
  EXPECT_EQ(tap(Mode::Swipe, MenuMode::Tap, 10, 400), TapAction::None);
  EXPECT_EQ(tap(Mode::Swipe, MenuMode::Tap, W - 10, 400), TapAction::None);
  EXPECT_EQ(tap(Mode::Swipe, MenuMode::Tap, W / 2, H / 2), TapAction::Menu);
}

TEST(ReaderTouch, OffIgnoresEveryTapIncludingTheMenu) {
  EXPECT_EQ(tap(Mode::Off, MenuMode::Tap, 10, 400), TapAction::None);
  EXPECT_EQ(tap(Mode::Off, MenuMode::Tap, W / 2, H / 2), TapAction::None);
}

TEST(ReaderTouch, MenuOffLeavesTheCentreTapInert) {
  EXPECT_EQ(tap(Mode::Tap, MenuMode::Off, W / 2, H / 2), TapAction::None);
  EXPECT_EQ(tap(Mode::Tap, MenuMode::SwipeUp, W / 2, H / 2), TapAction::None);
}

TEST(ReaderTouch, PageTurnZonesWinOverTheMenuWhenTheyOverlap) {
  // A tap on the boundary belongs to the page-turn zone: the menu never eats a turn.
  EXPECT_EQ(tap(Mode::Tap, MenuMode::Tap, W / 3 - 1, H / 2), TapAction::Prev);
  EXPECT_EQ(tap(Mode::Tap, MenuMode::Tap, W - W / 3, H / 2), TapAction::Next);
}

TEST(ReaderTouch, OnlySwipeModeTurnsPagesOnASwipe) {
  EXPECT_TRUE(reader_touch::swipeTurnsPages(Mode::Swipe));
  EXPECT_FALSE(reader_touch::swipeTurnsPages(Mode::Tap));
  EXPECT_FALSE(reader_touch::swipeTurnsPages(Mode::InvertedTap));
  EXPECT_FALSE(reader_touch::swipeTurnsPages(Mode::Off));
}

TEST(ReaderTouch, ADegenerateScreenNeverReportsAnAction) {
  EXPECT_EQ(reader_touch::tapAction(Mode::Tap, MenuMode::Tap, 0, 0, 0, 0), TapAction::None);
}

// A vertical swipe on the open page, as ReaderUtils reads it: the list-scroll bands decide
// whether it counts, the reader mode decides what it does.
TapAction vertical(Mode mode, int sx, int sy, int ex, int ey) {
  return reader_touch::scrollAction(mode, list_swipe::scrollFrom(W, H, sx, sy, ex, ey));
}

TEST(ReaderTouch, SwipeUpInTheBodyTurnsToTheNextPage) {
  EXPECT_EQ(vertical(Mode::Swipe, 240, 600, 240, 300), TapAction::Next);
}

TEST(ReaderTouch, SwipeDownInTheBodyTurnsToThePreviousPage) {
  EXPECT_EQ(vertical(Mode::Swipe, 240, 300, 240, 600), TapAction::Prev);
}

TEST(ReaderTouch, ATopEdgeDownSwipeStaysTheMenuGesture) {
  EXPECT_EQ(vertical(Mode::Swipe, 240, 40, 240, 400), TapAction::None);
}

TEST(ReaderTouch, ABottomEdgeUpSwipeStaysTheHomeGesture) {
  EXPECT_EQ(vertical(Mode::Swipe, 240, H - 40, 240, 300), TapAction::None);
}

TEST(ReaderTouch, VerticalSwipesTurnNothingOutsideSwipeMode) {
  EXPECT_EQ(vertical(Mode::Tap, 240, 600, 240, 300), TapAction::None);
  EXPECT_EQ(vertical(Mode::InvertedTap, 240, 300, 240, 600), TapAction::None);
  EXPECT_EQ(vertical(Mode::Off, 240, 600, 240, 300), TapAction::None);
}

TEST(ReaderTouch, AHorizontalSwipeIsNotAVerticalPageTurn) {
  EXPECT_EQ(vertical(Mode::Swipe, 100, 400, 400, 460), TapAction::None);
}

}  // namespace
