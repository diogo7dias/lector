#include <gtest/gtest.h>

#include "WatermarkBuildBail.h"

TEST(WatermarkBuildBail, YieldsWhileTheRequestedPageIsStillPastTheWatermark) {
  EXPECT_TRUE(watermarkBuildShouldYield(50, 40, false));
}

TEST(WatermarkBuildBail, DoesNotYieldOnceTheRequestedPageExists) {
  EXPECT_FALSE(watermarkBuildShouldYield(50, 51, false));
}

TEST(WatermarkBuildBail, DoesNotYieldWhenTheBuildHasFinished) { EXPECT_FALSE(watermarkBuildShouldYield(50, 40, true)); }

TEST(WatermarkBuildBail, ATickThatCrossesTheWaitingPageIsReadable) {
  EXPECT_TRUE(waitingPageBecameReadable(50, 50, 52));
}

TEST(WatermarkBuildBail, ATickAheadOfTheReaderDoesNotForceARepaint) {
  EXPECT_FALSE(waitingPageBecameReadable(10, 50, 52));
}

TEST(WatermarkBuildBail, ATickThatDoesNotReachTheWaitingPageStaysUnreadable) {
  EXPECT_FALSE(waitingPageBecameReadable(50, 40, 48));
}

// An exit leaves the partial only BUILD_WINDOW_AHEAD (5) pages past the reader. The old rule
// wanted 15 pages of slack before it would skip the rebuild, so every reopen blocked on it.
TEST(WatermarkBuildBail, AReopenedPartialThatHoldsThePageShowsItWithoutRebuilding) {
  EXPECT_TRUE(partialServesPage(6, 12));
  EXPECT_TRUE(partialServesPage(11, 12));
}

TEST(WatermarkBuildBail, ALandingPastThePartialStillBuilds) {
  EXPECT_FALSE(partialServesPage(12, 12));
  EXPECT_FALSE(partialServesPage(19, 12));
}

// Right after a turn the build stops BUILD_WINDOW_AHEAD (5) pages past the reader.
TEST(WatermarkBuildBail, ABuildWindowAheadOfABusyReaderWaits) {
  EXPECT_FALSE(backgroundBuildTickWanted(false, 8, 1, 5, false));
  EXPECT_TRUE(backgroundBuildTickWanted(false, 5, 1, 5, false));
}

// An idle reader's chapter builds to its end, so a short visit no longer leaves a partial
// a few pages past the reader that the next session lays out again from page 0.
TEST(WatermarkBuildBail, AnIdleReadersChapterBuildsPastTheWindow) {
  EXPECT_TRUE(backgroundBuildTickWanted(false, 8, 1, 5, true));
  EXPECT_TRUE(backgroundBuildTickWanted(false, 900, 1, 5, true));
}

TEST(WatermarkBuildBail, AnExtensionOfAPartialAlwaysBuilds) {
  EXPECT_TRUE(backgroundBuildTickWanted(true, 363, 0, 5, false));
}
