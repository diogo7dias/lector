#include <PerfStats.h>
#include <gtest/gtest.h>

#include "sleep/WakeFacePolicy.h"

TEST(WakeFacePolicy, RetiredSelectionsBecomeLightWithoutRenumberingOtherFaces) {
  EXPECT_EQ(wake_face::migrateSleepScreen(6), 1);  // saved Quick Resume -> Light
  EXPECT_EQ(wake_face::migrateSleepScreen(2), 2);  // Custom stays Custom
  EXPECT_EQ(wake_face::migrateSleepScreen(7), 7);  // Stats keeps its persisted ID
  EXPECT_EQ(wake_face::migrateSleepScreen(9), 9);  // Transparent keeps its persisted ID
  EXPECT_EQ(wake_face::migrateSleepScreen(0), 1);  // older retired Dark
  EXPECT_EQ(wake_face::migrateSleepScreen(5), 1);  // older retired Blank
  EXPECT_EQ(wake_face::migrateSleepScreen(8), 1);  // older retired Freeze
}

TEST(WakeFacePolicy, FastUnlockShortensOnlyTheLadderSettleWindow) {
  EXPECT_EQ(wake_face::inputSettleMs(false, true), 100ul);
  EXPECT_EQ(wake_face::inputSettleMs(false, false), 500ul);
  EXPECT_EQ(wake_face::inputSettleMs(true, true), 20ul);  // X4 Pro: digital inputs, no ladder
  EXPECT_EQ(wake_face::inputSettleMs(true, false), 20ul);
}

TEST(WakeFacePolicy, DifferentialWakeOnlyForTheCrestFaceWithFastUnlockAndNoBanners) {
  using wake_face::WakeClear;
  using wake_face::wakeClearFor;
  EXPECT_EQ(wakeClearFor(true, true, true), WakeClear::Differential);
  // A wallpaper, cover, stats or overlay face is arbitrary content: keep the clearing pass.
  EXPECT_EQ(wakeClearFor(true, false, true), WakeClear::Blank);
  // Fast Unlock off restores the clearing pass even on the crest face.
  EXPECT_EQ(wakeClearFor(false, true, true), WakeClear::Blank);
  // Banners wanted: their blocking pass stays.
  EXPECT_EQ(wakeClearFor(true, true, false), WakeClear::Blank);
  EXPECT_EQ(wakeClearFor(false, false, false), WakeClear::Blank);
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
