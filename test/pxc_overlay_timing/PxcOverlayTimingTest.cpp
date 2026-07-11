#include <gtest/gtest.h>

#include "src/activities/boot_sleep/PxcOverlayTiming.h"

TEST(PxcOverlayTimingTest, FinalCompositeHidesOverlayFromVisibleBase) {
  EXPECT_FALSE(shouldDrawPxcOverlay(PxcOverlayTiming::FinalComposite, PxcOverlayStage::Base, true));
  EXPECT_TRUE(shouldDrawPxcOverlay(PxcOverlayTiming::FinalComposite, PxcOverlayStage::Lsb, true));
  EXPECT_TRUE(shouldDrawPxcOverlay(PxcOverlayTiming::FinalComposite, PxcOverlayStage::Msb, true));
}

TEST(PxcOverlayTimingTest, EveryPassPreservesExistingOverlayBehavior) {
  EXPECT_TRUE(shouldDrawPxcOverlay(PxcOverlayTiming::EveryPass, PxcOverlayStage::Base, true));
  EXPECT_TRUE(shouldDrawPxcOverlay(PxcOverlayTiming::EveryPass, PxcOverlayStage::Lsb, true));
  EXPECT_TRUE(shouldDrawPxcOverlay(PxcOverlayTiming::EveryPass, PxcOverlayStage::Msb, true));
}

TEST(PxcOverlayTimingTest, ViewerUsesReliableEveryPassMode) {
  EXPECT_EQ(pxcViewerOverlayTiming(), PxcOverlayTiming::EveryPass);
}

TEST(PxcOverlayTimingTest, OneBitRenderStillDrawsOverlay) {
  EXPECT_TRUE(shouldDrawPxcOverlay(PxcOverlayTiming::FinalComposite, PxcOverlayStage::Base, false));
}

TEST(PxcOverlayTimingTest, FinalCompositeForcesBinaryHintRenderingInGrayPlanes) {
  EXPECT_FALSE(shouldForceBwPxcOverlay(PxcOverlayTiming::FinalComposite, PxcOverlayStage::Base, true));
  EXPECT_TRUE(shouldForceBwPxcOverlay(PxcOverlayTiming::FinalComposite, PxcOverlayStage::Lsb, true));
  EXPECT_TRUE(shouldForceBwPxcOverlay(PxcOverlayTiming::FinalComposite, PxcOverlayStage::Msb, true));
}
