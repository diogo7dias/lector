#include <gtest/gtest.h>

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

}  // namespace
