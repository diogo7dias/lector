#include <gtest/gtest.h>

#include "components/HintBandGeometry.h"

// A hint-band tap is one touch event standing for a two-edge button stroke. These pin
// the order the edges come out in, which is what every screen on top of the band relies
// on: press on the tap frame, release on the next, whichever edge the screen asks for
// first.

namespace {

constexpr int kBack = 0;
constexpr int kConfirm = 1;
constexpr bool kPress = false;
constexpr bool kRelease = true;

}  // namespace

TEST(TapStroke, ReleaseFirstQueryStillGetsTheNextFrameRelease) {
  // A release-only screen (UiListActivity's Back, ButtonNavigator's Up/Down) never asks
  // for the press. Its first sighting of the tap must still arm the stroke.
  hint_band::TapStroke stroke;
  EXPECT_FALSE(stroke.query(kBack, kBack, kRelease, 100));  // tap frame stays press-only
  stroke.tapOver();
  EXPECT_TRUE(stroke.query(kBack, -1, kRelease, 110));
  EXPECT_FALSE(stroke.query(kBack, -1, kRelease, 120)) << "one tap owes exactly one release";
}

TEST(TapStroke, PressOnTheTapFrameReleaseOnTheNext) {
  hint_band::TapStroke stroke;
  EXPECT_TRUE(stroke.query(kBack, kBack, kPress, 100));
  EXPECT_FALSE(stroke.query(kBack, kBack, kPress, 100)) << "one tap is one press";
  EXPECT_FALSE(stroke.query(kBack, kBack, kRelease, 100)) << "press and release never share a frame";
  stroke.tapOver();
  EXPECT_FALSE(stroke.query(kBack, -1, kPress, 110));
  EXPECT_TRUE(stroke.query(kBack, -1, kRelease, 110));
}

TEST(TapStroke, OnlyTheTappedHardwareAnswers) {
  hint_band::TapStroke stroke;
  EXPECT_FALSE(stroke.query(kConfirm, kBack, kPress, 100));
  EXPECT_TRUE(stroke.query(kBack, kBack, kPress, 100)) << "a miss on another key does not spend the tap";
  stroke.tapOver();
  EXPECT_FALSE(stroke.query(kConfirm, -1, kRelease, 110));
  EXPECT_TRUE(stroke.query(kBack, -1, kRelease, 110));
}

TEST(TapStroke, UnclaimedReleaseExpires) {
  hint_band::TapStroke stroke;
  EXPECT_TRUE(stroke.query(kBack, kBack, kPress, 100));
  stroke.tapOver();
  EXPECT_FALSE(stroke.query(kBack, -1, kRelease, 100 + hint_band::TapStroke::kReleaseWindowMs + 1));
}

TEST(TapStroke, ScreenChangeDropsTheOwedRelease) {
  // The press opened or closed a screen; its release belongs to that press, not to
  // whatever the screen now on top has selected.
  hint_band::TapStroke stroke;
  EXPECT_TRUE(stroke.query(kConfirm, kConfirm, kPress, 100));
  stroke.clear();
  stroke.tapOver();
  EXPECT_FALSE(stroke.query(kConfirm, -1, kRelease, 110));
}

TEST(TapStroke, NextTapStartsUnspent) {
  hint_band::TapStroke stroke;
  EXPECT_TRUE(stroke.query(kBack, kBack, kPress, 100));
  stroke.tapOver();
  EXPECT_TRUE(stroke.query(kBack, -1, kRelease, 110));
  EXPECT_TRUE(stroke.query(kBack, kBack, kPress, 400));
}
