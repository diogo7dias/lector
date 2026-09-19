#include <PerfStats.h>
#include <gtest/gtest.h>

#include "sleep/WakeFacePolicy.h"

TEST(WakeFacePolicy, OnlyWallpaperAndCoverSurviveMigration) {
  EXPECT_EQ(wake_face::migrateSleepScreen(2), 2);   // Custom stays Custom
  EXPECT_EQ(wake_face::migrateSleepScreen(3), 3);   // Cover stays Cover
  EXPECT_EQ(wake_face::migrateSleepScreen(0), 2);   // Dark
  EXPECT_EQ(wake_face::migrateSleepScreen(1), 2);   // Light (the crest)
  EXPECT_EQ(wake_face::migrateSleepScreen(4), 2);   // Cover + Custom
  EXPECT_EQ(wake_face::migrateSleepScreen(5), 2);   // Blank
  EXPECT_EQ(wake_face::migrateSleepScreen(6), 2);   // Quick Resume
  EXPECT_EQ(wake_face::migrateSleepScreen(7), 2);   // Stats Dashboard
  EXPECT_EQ(wake_face::migrateSleepScreen(8), 2);   // Freeze
  EXPECT_EQ(wake_face::migrateSleepScreen(9), 2);   // Transparent
  EXPECT_EQ(wake_face::migrateSleepScreen(42), 2);  // anything a future build wrote
}

TEST(WakeFacePolicy, FastUnlockShortensOnlyTheLadderSettleWindow) {
  EXPECT_EQ(wake_face::inputSettleMs(false, true), 100ul);
  EXPECT_EQ(wake_face::inputSettleMs(false, false), 500ul);
  EXPECT_EQ(wake_face::inputSettleMs(true, true), 20ul);  // X4 Pro: digital inputs, no ladder
  EXPECT_EQ(wake_face::inputSettleMs(true, false), 20ul);
}

TEST(WakeFacePolicy, DriveAllWakeOnlyWithFastUnlock) {
  using wake_face::WakeClear;
  using wake_face::wakeClearFor;
  EXPECT_EQ(wakeClearFor(true), WakeClear::DriveAll);
  // Fast Unlock off restores the clearing pass.
  EXPECT_EQ(wakeClearFor(false), WakeClear::Blank);
}

TEST(WakeFacePolicy, X4ProTakesTheSameDriveAllPathAsTheX4) {
  // The X4 Pro (SSD1677 on early batches, UC8179 on later ones) has no firmware branch
  // of its own on this path: the board split lives in the panel driver, and both drivers
  // honour the same one-shot. The policy answer for a non-X3 board is the X4's.
  using wake_face::WakeClear;
  constexpr bool kIsX3 = false;  // an X4 Pro, either batch
  EXPECT_EQ(wake_face::wakeClearFor(true), WakeClear::DriveAll);
  EXPECT_TRUE(wake_face::firstPageTurnCleans(WakeClear::DriveAll, kIsX3));
  EXPECT_EQ(wake_face::inputSettleMs(/*isX4Pro=*/true, true), 20ul);
}

TEST(WakeFacePolicy, FirstPageTurnCleansAfterADriveAllWakeExceptOnTheX3) {
  using wake_face::WakeClear;
  EXPECT_TRUE(wake_face::firstPageTurnCleans(WakeClear::DriveAll, false));
  EXPECT_FALSE(wake_face::firstPageTurnCleans(WakeClear::DriveAll, true));  // X3 first paint was the scrub
  EXPECT_FALSE(wake_face::firstPageTurnCleans(WakeClear::Blank, false));    // the blank already cleaned
  EXPECT_FALSE(wake_face::firstPageTurnCleans(WakeClear::Blank, true));
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
