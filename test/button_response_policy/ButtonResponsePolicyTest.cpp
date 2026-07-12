#include <gtest/gtest.h>

#include "src/util/ButtonResponsePolicy.h"

TEST(ButtonResponsePolicyTest, NavigationRespondsOnPress) {
  EXPECT_EQ(button_response::navigationTrigger(), button_response::Trigger::Press);
}

TEST(ButtonResponsePolicyTest, ImageMoveRespondsOnRelease) {
  EXPECT_EQ(button_response::imageMoveTrigger(), button_response::Trigger::Release);
}

TEST(ButtonResponsePolicyTest, PressOnlyDoesNotTriggerImageMove) {
  const bool pressOnlyTriggersMove = button_response::imageMoveTrigger() == button_response::Trigger::Press;
  EXPECT_FALSE(pressOnlyTriggersMove);
}

TEST(ButtonResponsePolicyTest, LongPressAwareActionsRespondOnRelease) {
  EXPECT_EQ(button_response::longPressAwareTrigger(), button_response::Trigger::Release);
}

TEST(ButtonResponsePolicyTest, GrabQuoteKeepsNextPersistedValue) {
  EXPECT_EQ(button_response::kGrabQuoteLongPressSettingValue, 3);
}

TEST(ButtonResponsePolicyTest, GrabQuoteStartsAtBookmarkHoldThreshold) {
  EXPECT_FALSE(button_response::shouldStartGrabQuote(3, 399));
  EXPECT_TRUE(button_response::shouldStartGrabQuote(3, 400));
  EXPECT_FALSE(button_response::shouldStartGrabQuote(2, 400));
}
