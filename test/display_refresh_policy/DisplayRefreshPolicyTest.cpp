#include <gtest/gtest.h>

#include "DisplayRefreshPolicy.h"

using Mode = DisplayRefreshPolicy::Mode;

TEST(DisplayRefreshPolicy, PromotesFirstFastRefreshAfterLongIdle) {
  DisplayRefreshPolicy policy;

  EXPECT_EQ(policy.choose(Mode::Fast, 1000), Mode::Fast);
  EXPECT_EQ(policy.choose(Mode::Fast, 61001), Mode::Clean);
}

TEST(DisplayRefreshPolicy, BoundsConsecutiveFastRefreshes) {
  DisplayRefreshPolicy policy;

  for (uint8_t i = 0; i < DisplayRefreshPolicy::MAX_CONSECUTIVE_FAST; ++i) {
    EXPECT_EQ(policy.choose(Mode::Fast, 1000 + i), Mode::Fast);
  }
  EXPECT_EQ(policy.choose(Mode::Fast, 2000), Mode::Clean);
}

TEST(DisplayRefreshPolicy, ExplicitCleanRefreshResetsFastBudget) {
  DisplayRefreshPolicy policy;

  for (uint8_t i = 0; i < DisplayRefreshPolicy::MAX_CONSECUTIVE_FAST; ++i) {
    EXPECT_EQ(policy.choose(Mode::Fast, 1000 + i), Mode::Fast);
  }
  EXPECT_EQ(policy.choose(Mode::Clean, 2000), Mode::Clean);
  EXPECT_EQ(policy.choose(Mode::Fast, 2001), Mode::Fast);
}

TEST(DisplayRefreshPolicy, FullRefreshPassesThroughAndResetsFastBudget) {
  DisplayRefreshPolicy policy;

  EXPECT_EQ(policy.choose(Mode::Fast, 1000), Mode::Fast);
  EXPECT_EQ(policy.choose(Mode::Full, 1001), Mode::Full);
  EXPECT_EQ(policy.choose(Mode::Fast, 1002), Mode::Fast);
}
