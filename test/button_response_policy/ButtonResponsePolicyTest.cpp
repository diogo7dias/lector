#include <gtest/gtest.h>

#include "src/util/ButtonResponsePolicy.h"

TEST(ButtonResponsePolicyTest, NavigationRespondsOnPress) {
  EXPECT_EQ(button_response::navigationTrigger(), button_response::Trigger::Press);
}

TEST(ButtonResponsePolicyTest, ImageMoveRespondsOnPress) {
  EXPECT_EQ(button_response::imageMoveTrigger(), button_response::Trigger::Press);
}

TEST(ButtonResponsePolicyTest, LongPressAwareActionsRespondOnRelease) {
  EXPECT_EQ(button_response::longPressAwareTrigger(), button_response::Trigger::Release);
}
