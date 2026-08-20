#include <gtest/gtest.h>

#include "HttpRangePolicy.h"

using namespace http_range;

TEST(HttpRangePolicy, OnlyTwoStatusesCarryABody) {
  EXPECT_TRUE(isBodyStatus(200, 0));
  EXPECT_TRUE(isBodyStatus(206, 400));
  EXPECT_FALSE(isBodyStatus(302, 0));
  EXPECT_FALSE(isBodyStatus(404, 0));
  EXPECT_FALSE(isBodyStatus(416, 400));
}

TEST(HttpRangePolicy, PartialContentIsRefusedWhenNoRangeWasAskedFor) {
  // Nothing asked for a slice, so a slice is not the file: writing it would
  // leave a body that is short but reads as complete.
  EXPECT_FALSE(isBodyStatus(206, 0));
}

TEST(HttpRangePolicy, RangeIsCompleteOnlyForARangedRequest) {
  EXPECT_TRUE(isRangeAlreadyComplete(416, 1000));
  // A 416 without a Range header of our own is a server fault, not a finished file.
  EXPECT_FALSE(isRangeAlreadyComplete(416, 0));
  EXPECT_FALSE(isRangeAlreadyComplete(200, 1000));
}

TEST(HttpRangePolicy, PlainDownloadWritesFromTheStart) {
  const BodyStart plan = planBodyStart(200, 0, true, 1500);
  EXPECT_FALSE(plan.discardPartial);
  EXPECT_EQ(plan.writeOffset, 0u);
  EXPECT_EQ(plan.total, 1500u);
}

TEST(HttpRangePolicy, PartialContentAppendsAndCountsTheBytesAlreadyHeld) {
  const BodyStart plan = planBodyStart(206, 400, true, 1100);
  EXPECT_FALSE(plan.discardPartial);
  EXPECT_EQ(plan.writeOffset, 400u);
  EXPECT_EQ(plan.total, 1500u);
}

TEST(HttpRangePolicy, IgnoredRangeThrowsThePartialAway) {
  // The server answered a resumed request with the whole file: the 400 bytes on
  // disk are the head of that same body and must not be written in front of it.
  const BodyStart plan = planBodyStart(200, 400, true, 1500);
  EXPECT_TRUE(plan.discardPartial);
  EXPECT_EQ(plan.writeOffset, 0u);
  EXPECT_EQ(plan.total, 1500u);
}

TEST(HttpRangePolicy, NoContentLengthLeavesTheTotalUnknown) {
  EXPECT_EQ(planBodyStart(200, 0, false, 0).total, 0u);
  EXPECT_EQ(planBodyStart(206, 400, false, 0).total, 0u);
  EXPECT_EQ(planBodyStart(206, 400, false, 0).writeOffset, 400u);
}
