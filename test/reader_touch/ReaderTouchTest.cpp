#include <gtest/gtest.h>

#include "activities/reader/ReaderEdgeSlider.h"
#include "activities/reader/ReaderTouchZones.h"

namespace {

using reader_touch::Gesture;
using reader_touch::MenuMode;
using reader_touch::TapAction;
using reader_touch::TapZoneLayout;

// The X4 Pro reading surface: 480 px wide, 800 tall.
constexpr int W = 480;
constexpr int H = 800;

TapAction tap(Gesture next, Gesture prev, int x, int y, MenuMode menu = MenuMode::Tap, bool rtl = false, bool on = true,
              TapZoneLayout layout = TapZoneLayout::RightHanded) {
  return reader_touch::tapAction(on, next, prev, menu, rtl, W, H, x, y, layout);
}
TapAction tapBoth(Gesture g, int x, int y, MenuMode menu = MenuMode::Tap, bool rtl = false,
                  TapZoneLayout layout = TapZoneLayout::RightHanded) {
  return tap(g, g, x, y, menu, rtl, true, layout);
}

TEST(ReaderTouch, TapTurnsPagesWithNoDeadZone) {
  EXPECT_EQ(tapBoth(Gesture::TapOnly, 10, 400), TapAction::Prev);
  EXPECT_EQ(tapBoth(Gesture::TapAndSwipe, W - 10, 400), TapAction::Next);
  // Top-center and bottom-center were previously a 22% dead zone returning None.
  EXPECT_EQ(tapBoth(Gesture::TapOnly, W / 2, 20), TapAction::Next);
  EXPECT_EQ(tapBoth(Gesture::TapOnly, W / 2, H - 20), TapAction::Next);
}

TEST(ReaderTouch, EveryLayoutCoversFullScreenWithNoDeadZone) {
  const TapZoneLayout layouts[] = {TapZoneLayout::RightHanded, TapZoneLayout::LeftHanded, TapZoneLayout::TopBottom,
                                   TapZoneLayout::EqualThirds};
  for (const auto layout : layouts) {
    for (const auto menu : {MenuMode::Tap, MenuMode::Off}) {
      for (int y = 0; y < H; y += 20) {
        for (int x = 0; x < W; x += 20) {
          EXPECT_NE(tapBoth(Gesture::TapOnly, x, y, menu, false, layout), TapAction::None)
              << "layout=" << static_cast<int>(layout) << " x=" << x << " y=" << y;
        }
      }
    }
  }
}

TEST(ReaderTouch, RightHandedSplitsAt25And75Percent) {
  EXPECT_EQ(tapBoth(Gesture::TapOnly, 119, 400, MenuMode::Tap, false, TapZoneLayout::RightHanded), TapAction::Prev);
  EXPECT_EQ(tapBoth(Gesture::TapOnly, 120, 100, MenuMode::Tap, false, TapZoneLayout::RightHanded), TapAction::Next);
  EXPECT_EQ(tapBoth(Gesture::TapOnly, W / 2, H / 2, MenuMode::Tap, false, TapZoneLayout::RightHanded), TapAction::Menu);
}

TEST(ReaderTouch, LeftHandedSplitsAt75And25Percent) {
  EXPECT_EQ(tapBoth(Gesture::TapOnly, 10, 400, MenuMode::Tap, false, TapZoneLayout::LeftHanded), TapAction::Next);
  EXPECT_EQ(tapBoth(Gesture::TapOnly, 359, 100, MenuMode::Tap, false, TapZoneLayout::LeftHanded), TapAction::Next);
  EXPECT_EQ(tapBoth(Gesture::TapOnly, 360, 400, MenuMode::Tap, false, TapZoneLayout::LeftHanded), TapAction::Prev);
  EXPECT_EQ(tapBoth(Gesture::TapOnly, W / 2, H / 2, MenuMode::Tap, false, TapZoneLayout::LeftHanded), TapAction::Menu);
  // RTL swaps left-handed zones cleanly.
  EXPECT_EQ(tapBoth(Gesture::TapOnly, 10, 400, MenuMode::Tap, true, TapZoneLayout::LeftHanded), TapAction::Prev);
  EXPECT_EQ(tapBoth(Gesture::TapOnly, 400, 400, MenuMode::Tap, true, TapZoneLayout::LeftHanded), TapAction::Next);
}

TEST(ReaderTouch, TopBottomSplitsAtMidHeight) {
  EXPECT_EQ(tapBoth(Gesture::TapOnly, 100, 100, MenuMode::Tap, false, TapZoneLayout::TopBottom), TapAction::Prev);
  EXPECT_EQ(tapBoth(Gesture::TapOnly, 380, 399, MenuMode::Tap, false, TapZoneLayout::TopBottom), TapAction::Prev);
  EXPECT_EQ(tapBoth(Gesture::TapOnly, 100, 400, MenuMode::Tap, false, TapZoneLayout::TopBottom), TapAction::Next);
  EXPECT_EQ(tapBoth(Gesture::TapOnly, W / 2, H / 2, MenuMode::Tap, false, TapZoneLayout::TopBottom), TapAction::Menu);
  EXPECT_EQ(tapBoth(Gesture::InvertedTap, 100, 100, MenuMode::Tap, false, TapZoneLayout::TopBottom), TapAction::Next);
}

TEST(ReaderTouch, EqualThirdsHasThreeEqualVerticalColumns) {
  EXPECT_EQ(tapBoth(Gesture::TapOnly, 159, 20, MenuMode::Tap, false, TapZoneLayout::EqualThirds), TapAction::Prev);
  EXPECT_EQ(tapBoth(Gesture::TapOnly, 160, 20, MenuMode::Tap, false, TapZoneLayout::EqualThirds), TapAction::Menu);
  EXPECT_EQ(tapBoth(Gesture::TapOnly, 319, H - 20, MenuMode::Tap, false, TapZoneLayout::EqualThirds), TapAction::Menu);
  EXPECT_EQ(tapBoth(Gesture::TapOnly, 320, 20, MenuMode::Tap, false, TapZoneLayout::EqualThirds), TapAction::Next);
  // With MenuMode::Off, the middle third splits cleanly at W/2 rather than leaving a dead zone.
  EXPECT_EQ(tapBoth(Gesture::TapOnly, 200, H / 2, MenuMode::Off, false, TapZoneLayout::EqualThirds), TapAction::Prev);
  EXPECT_EQ(tapBoth(Gesture::TapOnly, 280, H / 2, MenuMode::Off, false, TapZoneLayout::EqualThirds), TapAction::Next);
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

TEST(ReaderTouch, MenuOffDisablesMenuTap) {
  EXPECT_NE(tapBoth(Gesture::TapOnly, W / 2, H / 2, MenuMode::Off), TapAction::Menu);
  EXPECT_EQ(tapBoth(Gesture::SwipeOnly, W / 2, H / 2, MenuMode::Off), TapAction::None);
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

TEST(ReaderTouch, TopRightCornerTapTogglesBookmarkOnlyWhenEnabled) {
  using reader_touch::BOOKMARK_CORNER_H;
  using reader_touch::BOOKMARK_CORNER_W;
  using reader_touch::isBookmarkCornerTap;
  EXPECT_TRUE(isBookmarkCornerTap(true, W, H, W - 1, 0));
  EXPECT_TRUE(isBookmarkCornerTap(true, W, H, W - BOOKMARK_CORNER_W, BOOKMARK_CORNER_H - 1));
  EXPECT_FALSE(isBookmarkCornerTap(true, W, H, W - BOOKMARK_CORNER_W - 1, 10));
  EXPECT_FALSE(isBookmarkCornerTap(true, W, H, W - 10, BOOKMARK_CORNER_H));
  EXPECT_FALSE(isBookmarkCornerTap(false, W, H, W - 10, 10));
  EXPECT_FALSE(isBookmarkCornerTap(true, 0, H, 0, 0));
}

TEST(ReaderEdgeSlider, LeftEdgeVerticalDragAdjustsBrightnessAndSavesOnceOnLift) {
  ReaderEdgeSlider slider;
  // Touch down in left 12% strip (x=20 < 57).
  auto s0 = slider.update(20, 500, W, H, true, true, 40, 100, 50);
  EXPECT_FALSE(s0.active);
  EXPECT_FALSE(slider.isDragging());

  // Move up past threshold (dy=30 >= 24): arms drag.
  auto s1 = slider.update(20, 470, W, H, true, true, 40, 100, 50);
  EXPECT_TRUE(s1.active);
  EXPECT_TRUE(slider.isDragging());
  EXPECT_EQ(s1.axis, ReaderEdgeSlider::Axis::Brightness);

  // Continue dragging up 240 px (half of 480 px span -> +50 brightness, clamped to 90).
  auto s2 = slider.update(22, 230, W, H, true, true, 40, 100, 50);
  EXPECT_TRUE(s2.active);
  EXPECT_TRUE(s2.valueChanged);
  EXPECT_EQ(s2.value, 90);

  // Respects maxBrightness ceiling.
  auto s3 = slider.update(22, 100, W, H, true, true, 90, 80, 50);
  EXPECT_TRUE(s3.active);
  EXPECT_LE(s3.value, 100);

  // Lift requests one settings save; subsequent finish does not.
  EXPECT_TRUE(slider.finish());
  EXPECT_FALSE(slider.isDragging());
  EXPECT_FALSE(slider.finish());
}

TEST(ReaderEdgeSlider, LeftEdgeDragUpFromOffTurnsLightOn) {
  ReaderEdgeSlider slider;
  slider.update(15, 600, W, H, true, false, 60, 100, 50);
  slider.update(15, 570, W, H, true, false, 60, 100, 50);
  auto s = slider.update(15, 330, W, H, true, false, 60, 100, 50);
  EXPECT_TRUE(s.active);
  EXPECT_TRUE(s.valueChanged);
  EXPECT_TRUE(s.turnOn);
  EXPECT_EQ(s.value, 50);
  EXPECT_TRUE(slider.finish());
}

TEST(ReaderEdgeSlider, RightEdgeVerticalDragAdjustsWarmthOnlyWhenSupported) {
  ReaderEdgeSlider slider;
  slider.update(W - 20, 500, W, H, true, true, 50, 100, 60);
  auto s1 = slider.update(W - 20, 530, W, H, true, true, 50, 100, 60);
  EXPECT_TRUE(s1.active);
  EXPECT_TRUE(slider.isDragging());
  EXPECT_EQ(s1.axis, ReaderEdgeSlider::Axis::Warmth);

  // Dragging down reduces warmth.
  auto s2 = slider.update(W - 20, 770, W, H, true, true, 50, 100, 60);
  EXPECT_TRUE(s2.valueChanged);
  EXPECT_EQ(s2.value, 10);
  EXPECT_TRUE(slider.finish());

  // Without colour temperature hardware, right-edge drags are ignored.
  slider.update(W - 20, 500, W, H, false, true, 50, 100, 60);
  auto ignored = slider.update(W - 20, 300, W, H, false, true, 50, 100, 60);
  EXPECT_FALSE(ignored.active);
  EXPECT_FALSE(slider.isDragging());
  EXPECT_FALSE(slider.finish());
}

TEST(ReaderEdgeSlider, HorizontalSwipeAndCentreAndMarginsAreIgnored) {
  ReaderEdgeSlider slider;
  // Right-to-left page swipe starting inside right 12% strip must not be stolen.
  slider.update(W - 20, 420, W, H, true, true, 50, 100, 50);
  auto horiz = slider.update(W - 60, 425, W, H, true, true, 50, 100, 50);
  EXPECT_FALSE(horiz.active);
  EXPECT_FALSE(slider.isDragging());
  // Even if the finger later drifts vertically, the contact stays rejected until lift.
  auto drift = slider.update(W - 60, 200, W, H, true, true, 50, 100, 50);
  EXPECT_FALSE(drift.active);
  EXPECT_FALSE(slider.finish());

  // Centre strip drag is ignored.
  slider.update(W / 2, 500, W, H, true, true, 50, 100, 50);
  EXPECT_FALSE(slider.update(W / 2, 300, W, H, true, true, 50, 100, 50).active);
  EXPECT_FALSE(slider.finish());

  // Top/bottom 15% margin starts are ignored: the Light Panel pull-down and the reader menu
  // swipe-up both start within 14% of their edge, and must not also move the light.
  slider.update(20, H * 14 / 100, W, H, true, true, 50, 100, 50);
  EXPECT_FALSE(slider.update(20, H * 14 / 100 + 200, W, H, true, true, 50, 100, 50).active);
  EXPECT_FALSE(slider.finish());
  slider.update(20, H - H * 14 / 100, W, H, true, true, 50, 100, 50);
  EXPECT_FALSE(slider.update(20, H - H * 14 / 100 - 200, W, H, true, true, 50, 100, 50).active);
  EXPECT_FALSE(slider.finish());
  slider.update(20, 40, W, H, true, true, 50, 100, 50);
  EXPECT_FALSE(slider.update(20, 300, W, H, true, true, 50, 100, 50).active);
  EXPECT_FALSE(slider.finish());

  slider.update(20, H - 40, W, H, true, true, 50, 100, 50);
  EXPECT_FALSE(slider.update(20, 400, W, H, true, true, 50, 100, 50).active);
  EXPECT_FALSE(slider.finish());

  // Plain tap inside edge strip does not arm drag or save.
  slider.update(20, 400, W, H, true, true, 50, 100, 50);
  EXPECT_FALSE(slider.update(20, 405, W, H, true, true, 50, 100, 50).active);
  EXPECT_FALSE(slider.finish());
}

}  // namespace
