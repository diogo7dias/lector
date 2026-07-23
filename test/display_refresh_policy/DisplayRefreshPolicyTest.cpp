#include <gtest/gtest.h>

#include "DisplayRefreshPolicy.h"

using Mode = DisplayRefreshPolicy::Mode;

TEST(DisplayRefreshPolicy, DoesNotPromoteFastAfterLongIdle) {
  DisplayRefreshPolicy policy;

  // On an e-reader "idle" is the user reading the current page, which routinely
  // exceeds a minute. Idle time must NOT trigger a clean, or nearly every real
  // page turn is promoted from a fast async refresh to a slow blocking one.
  EXPECT_EQ(policy.choose(Mode::Fast, 1000), Mode::Fast);
  EXPECT_EQ(policy.choose(Mode::Fast, 5UL * 60000), Mode::Fast);
  EXPECT_EQ(policy.choose(Mode::Fast, 20UL * 60000), Mode::Fast);
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
