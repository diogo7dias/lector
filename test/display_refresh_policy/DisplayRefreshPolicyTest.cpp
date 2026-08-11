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

// A boot inherits whatever the panel was already holding, and a differential FAST leaves all
// of it in place. forceNextFull() is how the boot path starts from a clean panel instead.
TEST(DisplayRefreshPolicy, ForcedFullPromotesTheNextFastRefresh) {
  DisplayRefreshPolicy policy;

  policy.forceNextFull();
  EXPECT_EQ(policy.choose(Mode::Fast, 1000), Mode::Full);
}

TEST(DisplayRefreshPolicy, ForcedFullAppliesOnlyOnce) {
  DisplayRefreshPolicy policy;

  policy.forceNextFull();
  EXPECT_EQ(policy.choose(Mode::Fast, 1000), Mode::Full);
  EXPECT_EQ(policy.choose(Mode::Fast, 1001), Mode::Fast);
  EXPECT_EQ(policy.choose(Mode::Fast, 1002), Mode::Fast);
}

TEST(DisplayRefreshPolicy, ForcedFullOutranksAnExplicitCleanRequest) {
  DisplayRefreshPolicy policy;

  policy.forceNextFull();
  EXPECT_EQ(policy.choose(Mode::Clean, 1000), Mode::Full);
}

// The forced pass really does discharge the panel, so it counts as the FULL the escalation
// budget was waiting for: the next FAST run starts from a full budget, not a stale one.
TEST(DisplayRefreshPolicy, ForcedFullResetsTheFastBudget) {
  DisplayRefreshPolicy policy;

  for (uint8_t i = 0; i < DisplayRefreshPolicy::MAX_CONSECUTIVE_FAST; ++i) {
    EXPECT_EQ(policy.choose(Mode::Fast, 1000 + i), Mode::Fast);
  }
  policy.forceNextFull();
  EXPECT_EQ(policy.choose(Mode::Fast, 2000), Mode::Full);
  for (uint8_t i = 0; i < DisplayRefreshPolicy::MAX_CONSECUTIVE_FAST; ++i) {
    EXPECT_EQ(policy.choose(Mode::Fast, 3000 + i), Mode::Fast);
  }
  EXPECT_EQ(policy.choose(Mode::Fast, 4000), Mode::Clean);
}
