#include <gtest/gtest.h>

#include <cstdint>

#include "src/activities/reader/ReaderPinchGesture.h"

TEST(ReaderPinchGesture, FirstTwoContactFrameOnlyInitializes) {
  ReaderPinchGesture gesture;
  EXPECT_EQ(gesture.update(0, 0, 100, 0), ReaderPinchGesture::Action::None);
  EXPECT_TRUE(gesture.isActive());
}

TEST(ReaderPinchGesture, RequiresRelativeAndAbsoluteMovement) {
  ReaderPinchGesture gesture;
  gesture.update(0, 0, 100, 0);
  EXPECT_EQ(gesture.update(0, 0, 105, 0), ReaderPinchGesture::Action::None);
  EXPECT_EQ(gesture.update(0, 0, 110, 0), ReaderPinchGesture::Action::None);
  EXPECT_EQ(gesture.update(0, 0, 120, 0), ReaderPinchGesture::Action::None);
  EXPECT_EQ(gesture.update(0, 0, 122, 0), ReaderPinchGesture::Action::Increase);

  gesture.reset();
  gesture.update(0, 0, 10, 0);
  EXPECT_EQ(gesture.update(0, 0, 19, 0), ReaderPinchGesture::Action::None);
}

TEST(ReaderPinchGesture, ParallelTranslationLocksOutPinchUntilContactsEnd) {
  ReaderPinchGesture gesture;
  gesture.update(0, 0, 200, 0);

  EXPECT_EQ(gesture.update(30, 0, 230, 0), ReaderPinchGesture::Action::None);
  EXPECT_EQ(gesture.update(60, 0, 285, 0), ReaderPinchGesture::Action::None);

  gesture.reset();
  gesture.update(0, 0, 200, 0);
  EXPECT_EQ(gesture.update(60, 0, 240, 0), ReaderPinchGesture::Action::None);
}

TEST(ReaderPinchGesture, PinchWithStableMidpointStillResizes) {
  ReaderPinchGesture gesture;
  gesture.update(0, 0, 100, 0);
  EXPECT_EQ(gesture.update(-10, 0, 110, 0), ReaderPinchGesture::Action::None);
  EXPECT_EQ(gesture.update(-11, 0, 111, 0), ReaderPinchGesture::Action::Increase);
}

TEST(ReaderPinchGesture, SpreadFiresOnceUntilContactsEnd) {
  ReaderPinchGesture gesture;
  gesture.update(0, 0, 100, 0);
  EXPECT_EQ(gesture.update(0, 0, 125, 0), ReaderPinchGesture::Action::None);
  EXPECT_EQ(gesture.update(0, 0, 130, 0), ReaderPinchGesture::Action::Increase);
  EXPECT_EQ(gesture.update(0, 0, 150, 0), ReaderPinchGesture::Action::None);
  EXPECT_EQ(gesture.update(0, 0, 75, 0), ReaderPinchGesture::Action::None);

  gesture.reset();
  EXPECT_EQ(gesture.update(0, 0, 100, 0), ReaderPinchGesture::Action::None);
  EXPECT_EQ(gesture.update(0, 0, 125, 0), ReaderPinchGesture::Action::None);
  EXPECT_EQ(gesture.update(0, 0, 130, 0), ReaderPinchGesture::Action::Increase);
}

TEST(ReaderPinchGesture, ContractFiresAndIgnoresContactOrder) {
  ReaderPinchGesture gesture;
  gesture.update(0, 0, 200, 0);
  EXPECT_EQ(gesture.update(150, 0, 0, 0), ReaderPinchGesture::Action::None);
  EXPECT_EQ(gesture.update(155, 0, 0, 0), ReaderPinchGesture::Action::Decrease);
}

TEST(ReaderPinchGesture, RotationWithScaleWobbleNeverResizesFont) {
  ReaderPinchGesture gesture;
  gesture.update(0, 0, 100, 0);

  EXPECT_EQ(gesture.update(3, -12, 107, 12), ReaderPinchGesture::Action::None);
  EXPECT_EQ(gesture.update(8, -25, 112, 25), ReaderPinchGesture::Action::None);
  EXPECT_EQ(gesture.update(-15, -50, 115, 50), ReaderPinchGesture::Action::None);
}

TEST(ReaderPinchGesture, SmallDirectionDriftStillAllowsPinch) {
  ReaderPinchGesture gesture;
  gesture.update(0, 0, 100, 0);

  EXPECT_EQ(gesture.update(0, 0, 120, 10), ReaderPinchGesture::Action::None);
  EXPECT_EQ(gesture.update(0, 0, 122, 11), ReaderPinchGesture::Action::Increase);
}

TEST(ReaderPinchGesture, OneNoisyScaleFrameDoesNotResizeFont) {
  ReaderPinchGesture gesture;
  gesture.update(0, 0, 100, 0);

  EXPECT_EQ(gesture.update(-12, 0, 112, 0), ReaderPinchGesture::Action::None);
  EXPECT_EQ(gesture.update(-4, 0, 104, 0), ReaderPinchGesture::Action::None);
}
