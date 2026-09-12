#include <gtest/gtest.h>

#include "sleep/WakeFacePolicy.h"

using wake_face::restoredFrameNeedsPush;
using wake_face::retainsPanelForWake;
using Face = wake_face::SleepFace;

// Quick Resume restores the exact frame the panel is already showing, so handing the
// panel over untouched is honest and saves the clearing pass.
TEST(WakeFacePolicy, QuickResumeFrameKeepsThePanel) { EXPECT_TRUE(retainsPanelForWake(Face::QuickResumeFrame)); }

// Regression, 0.21.0 (upstream #2943): a custom wallpaper was treated as retained
// boot content, which skipped the blank-and-FULL pass that clears it. A differential
// waveform only drives changed pixels, so the artwork stayed visible under the home
// screen and under every page that followed. The wake must clear.
TEST(WakeFacePolicy, CustomWallpaperDoesNotKeepThePanel) { EXPECT_FALSE(retainsPanelForWake(Face::CustomWallpaper)); }

// Anything else (cover, clock, blank) has no retained frame to hand over.
TEST(WakeFacePolicy, OtherFacesDoNotKeepThePanel) { EXPECT_FALSE(retainsPanelForWake(Face::Other)); }

// Quick Resume on an X4: the restored frame is what the glass already shows and the
// driver's first paint is a clean pass either way, so the wake pushes nothing.
TEST(WakeFacePolicy, X4QuickResumeSkipsTheFramePush) { EXPECT_FALSE(restoredFrameNeedsPush(false, false)); }

// Banners are new pixels on top of the frame; they have to reach the panel.
TEST(WakeFacePolicy, BannersForceTheFramePush) { EXPECT_TRUE(restoredFrameNeedsPush(false, true)); }

// The X3 keeps its differential push (upstream #2698), banners or not.
TEST(WakeFacePolicy, X3AlwaysPushesTheFrame) {
  EXPECT_TRUE(restoredFrameNeedsPush(true, false));
  EXPECT_TRUE(restoredFrameNeedsPush(true, true));
}
