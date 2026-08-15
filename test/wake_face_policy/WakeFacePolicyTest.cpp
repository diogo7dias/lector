#include <gtest/gtest.h>

#include "sleep/WakeFacePolicy.h"

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
