#include <gtest/gtest.h>

#include "WatermarkBuildBail.h"

TEST(WatermarkBuildBail, YieldsWhileTheRequestedPageIsStillPastTheWatermark) {
  EXPECT_TRUE(watermarkBuildShouldYield(50, 40, false));
}

TEST(WatermarkBuildBail, DoesNotYieldOnceTheRequestedPageExists) {
  EXPECT_FALSE(watermarkBuildShouldYield(50, 51, false));
}

TEST(WatermarkBuildBail, DoesNotYieldWhenTheBuildHasFinished) {
  EXPECT_FALSE(watermarkBuildShouldYield(50, 40, true));
}

TEST(WatermarkBuildBail, ATickThatCrossesTheWaitingPageIsReadable) {
  EXPECT_TRUE(waitingPageBecameReadable(50, 50, 52));
}

TEST(WatermarkBuildBail, ATickAheadOfTheReaderDoesNotForceARepaint) {
  EXPECT_FALSE(waitingPageBecameReadable(10, 50, 52));
}

TEST(WatermarkBuildBail, ATickThatDoesNotReachTheWaitingPageStaysUnreadable) {
  EXPECT_FALSE(waitingPageBecameReadable(50, 40, 48));
}
