#include <PerfStats.h>
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

// Fast and conservative paths must both preserve new banner pixels. Only the
// unchanged retained frame may skip a submission, on every supported board.
TEST(WakeFacePolicy, RetainedFrameFastAndConservative) {
  for (const bool x3 : {false, true}) {
    EXPECT_FALSE(restoredFrameNeedsPush(x3, false, true));
    EXPECT_EQ(restoredFrameNeedsPush(x3, false, false), x3);
    EXPECT_TRUE(restoredFrameNeedsPush(x3, true, true));
    EXPECT_TRUE(restoredFrameNeedsPush(x3, true, false));
  }
}

TEST(WakeFacePolicy, WakePanelCaptureStopsAtReadableAndBoundsOutput) {
  // B/W base and grayscale composite are two submissions, regardless of their
  // matching mode labels. Later page turns must not change the wake record.
  PerfStats::noteRefresh(2, 2, 617000, 0, 0);
  PerfStats::noteRefresh(2, 2, 500000, 0, 0);
  PerfStats::finishWakePanels();
  PerfStats::noteRefresh(1, 1, 1800000, 0, 0);
  EXPECT_EQ(PerfStats::wakePanelCount(), 2u);
  char out[48];
  PerfStats::formatWakePanels(out, sizeof(out));
  EXPECT_STREQ(out, "617/500");
  char tiny[2] = {};
  PerfStats::formatWakePanels(tiny, sizeof(tiny));
  EXPECT_EQ(tiny[1], '\0');
  PerfStats::formatWakePanels(nullptr, 0);
}
