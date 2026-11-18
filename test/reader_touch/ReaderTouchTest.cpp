#include <gtest/gtest.h>

#include "activities/reader/ReaderTouchZones.h"

namespace {

using reader_touch::Gesture;
using reader_touch::MenuMode;
using reader_touch::TapAction;

// The X4 Pro reading surface: 480 px wide, 800 tall.
constexpr int W = 480;
constexpr int H = 800;

TapAction tap(Gesture next, Gesture prev, int x, int y, MenuMode menu = MenuMode::Tap, bool rtl = false,
              bool on = true) {
  return reader_touch::tapAction(on, next, prev, menu, rtl, W, H, x, y);
}
TapAction tapBoth(Gesture g, int x, int y, MenuMode menu = MenuMode::Tap, bool rtl = false) {
  return tap(g, g, x, y, menu, rtl);
}

TEST(ReaderTouch, TapTurnsPagesFromTheOuterThirds) {
  EXPECT_EQ(tapBoth(Gesture::TapOnly, 10, 400), TapAction::Prev);
  EXPECT_EQ(tapBoth(Gesture::TapAndSwipe, W - 10, 400), TapAction::Next);
  EXPECT_EQ(tapBoth(Gesture::TapOnly, W / 2, 20), TapAction::None);
}

TEST(ReaderTouch, InvertedTapSwapsTheTwoSides) {
  EXPECT_EQ(tapBoth(Gesture::InvertedTap, 10, 400), TapAction::Next);
  EXPECT_EQ(tapBoth(Gesture::InvertedTap, W - 10, 400), TapAction::Prev);
  // Either direction on Inverted Tap swaps the zones both share.
  EXPECT_EQ(tap(Gesture::InvertedTap, Gesture::TapOnly, 10, 400), TapAction::Next);
}

TEST(ReaderTouch, TheCentreThirdOfBothAxesOpensTheMenu) {
  EXPECT_EQ(tapBoth(Gesture::TapOnly, W / 2, H / 2), TapAction::Menu);
  EXPECT_EQ(tapBoth(Gesture::SwipeOnly, W / 2, H / 2), TapAction::Menu);
}

TEST(ReaderTouch, SwipeOnlyLeavesEveryOtherTapQuiet) {
  EXPECT_EQ(tapBoth(Gesture::SwipeOnly, 10, 400), TapAction::None);
  EXPECT_EQ(tapBoth(Gesture::Disabled, W - 10, 400), TapAction::None);
}

TEST(ReaderTouch, TheSwitchOffIgnoresEveryTapIncludingTheMenu) {
  EXPECT_EQ(tap(Gesture::TapOnly, Gesture::TapOnly, 10, 400, MenuMode::Tap, false, false), TapAction::None);
  EXPECT_EQ(tap(Gesture::TapOnly, Gesture::TapOnly, W / 2, H / 2, MenuMode::Tap, false, false), TapAction::None);
}

TEST(ReaderTouch, MenuOffLeavesTheCentreTapInert) {
  EXPECT_EQ(tapBoth(Gesture::TapOnly, W / 2, H / 2, MenuMode::Off), TapAction::None);
  EXPECT_EQ(tapBoth(Gesture::TapOnly, W / 2, H / 2, MenuMode::SwipeUp), TapAction::None);
}

TEST(ReaderTouch, ASoleTapDirectionGetsTheWholePageButTheMenuCentre) {
  EXPECT_EQ(tap(Gesture::TapOnly, Gesture::SwipeOnly, 10, 400), TapAction::Next);
  EXPECT_EQ(tap(Gesture::TapOnly, Gesture::SwipeOnly, W / 2, 20), TapAction::Next);
  EXPECT_EQ(tap(Gesture::TapOnly, Gesture::SwipeOnly, W / 2, H / 2), TapAction::Menu);
  EXPECT_EQ(tap(Gesture::Disabled, Gesture::TapAndSwipe, W - 10, 400), TapAction::Prev);
}

TEST(ReaderTouch, ARightToLeftBookReversesTheZones) {
  EXPECT_EQ(tapBoth(Gesture::TapOnly, 10, 400, MenuMode::Tap, true), TapAction::Next);
  EXPECT_EQ(tapBoth(Gesture::TapOnly, W - 10, 400, MenuMode::Tap, true), TapAction::Prev);
  // Inverted Tap in a right-to-left book cancels out.
  EXPECT_EQ(tapBoth(Gesture::InvertedTap, 10, 400, MenuMode::Tap, true), TapAction::Prev);
}

TEST(ReaderTouch, SwipesFollowTheirOwnDirectionsGesture) {
  using reader_touch::swipeAction;
  EXPECT_EQ(swipeAction(true, Gesture::SwipeOnly, Gesture::SwipeOnly, false, true), TapAction::Next);
  EXPECT_EQ(swipeAction(true, Gesture::SwipeOnly, Gesture::SwipeOnly, false, false), TapAction::Prev);
  EXPECT_EQ(swipeAction(true, Gesture::TapOnly, Gesture::SwipeOnly, false, true), TapAction::None);
  EXPECT_EQ(swipeAction(true, Gesture::TapOnly, Gesture::SwipeOnly, false, false), TapAction::Prev);
  EXPECT_EQ(swipeAction(true, Gesture::TapAndSwipe, Gesture::Disabled, false, false), TapAction::None);
  EXPECT_EQ(swipeAction(false, Gesture::SwipeOnly, Gesture::SwipeOnly, false, true), TapAction::None);
}

TEST(ReaderTouch, ARightToLeftBookSwipesTheOtherWay) {
  using reader_touch::swipeAction;
  EXPECT_EQ(swipeAction(true, Gesture::SwipeOnly, Gesture::SwipeOnly, true, true), TapAction::Prev);
  EXPECT_EQ(swipeAction(true, Gesture::SwipeOnly, Gesture::SwipeOnly, true, false), TapAction::Next);
}

TEST(ReaderTouch, HebrewArabicAndPersianTagsAreRightToLeft) {
  using reader_touch::isRtlLanguage;
  for (const char* tag : {"he", "iw", "ar", "fa", "AR", "he-IL", "ar_EG", "fa-IR"})
    EXPECT_TRUE(isRtlLanguage(tag)) << tag;
  for (const char* tag : {"", "e", "en", "en-US", "heb", "ara", "hex", "fr", "ja"})
    EXPECT_FALSE(isRtlLanguage(tag)) << tag;
  EXPECT_FALSE(isRtlLanguage(nullptr));
}

TEST(ReaderTouch, ADegenerateScreenNeverReportsAnAction) {
  EXPECT_EQ(reader_touch::tapAction(true, Gesture::TapOnly, Gesture::TapOnly, MenuMode::Tap, false, 0, 0, 0, 0),
            TapAction::None);
}

TEST(ReaderTouch, TheOldCombinedModeSplitsIntoSwitchAndGesture) {
  using reader_touch::migrateLegacyMode;
  EXPECT_EQ(migrateLegacyMode(0).on, 0);
  EXPECT_EQ(migrateLegacyMode(1).on, 1);
  EXPECT_EQ(migrateLegacyMode(1).gesture, static_cast<uint8_t>(Gesture::TapOnly));
  EXPECT_EQ(migrateLegacyMode(2).gesture, static_cast<uint8_t>(Gesture::SwipeOnly));
  EXPECT_EQ(migrateLegacyMode(3).gesture, static_cast<uint8_t>(Gesture::InvertedTap));
  EXPECT_FALSE(migrateLegacyMode(4).valid);
  EXPECT_FALSE(migrateLegacyMode(255).valid);
}

}  // namespace
