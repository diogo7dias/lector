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

// ── Surviving a lock ────────────────────────────────────────────────────────
//
// Waking from deep sleep is a chip reset, so the FAST budget starts at zero every
// session. Before the counter was carried across the lock, a reader who slept the
// device every 20 pages could never reach MAX_FAST_BEFORE_FULL, and the only pass
// that genuinely discharges the panel never ran — the ghosting nobody could explain.

TEST(DisplayRefreshPolicy, SeedRestoresTheFastBudgetAcrossALock) {
  DisplayRefreshPolicy before;
  for (uint8_t i = 0; i < 20; ++i) before.choose(Mode::Fast, 1000 + i);
  // 19, not 20: the 13th request was promoted to Clean by MAX_CONSECUTIVE_FAST, and a
  // promoted pass is not a FAST pass, so it spends no discharge budget.
  EXPECT_EQ(before.fastSinceFull(), 19);

  // A wake builds a fresh policy: without the seed it would start over at zero.
  DisplayRefreshPolicy after;
  after.seedFastSinceFull(before.fastSinceFull());
  EXPECT_EQ(after.fastSinceFull(), 19);
}

TEST(DisplayRefreshPolicy, ASeededBudgetStillReachesTheFullRefresh) {
  DisplayRefreshPolicy policy;
  policy.seedFastSinceFull(DisplayRefreshPolicy::MAX_FAST_BEFORE_FULL - 1);

  // One short session after the lock is enough, because the budget carried over.
  EXPECT_EQ(policy.choose(Mode::Fast, 1000), Mode::Fast);
  EXPECT_EQ(policy.choose(Mode::Fast, 1001), Mode::Full);
  EXPECT_EQ(policy.fastSinceFull(), 0);
}

// A seed only restores the discharge budget, never the consecutive-FAST run: the
// panel was cleaned on the way into sleep, so the first page after a wake has no
// run behind it.
TEST(DisplayRefreshPolicy, ASeedDoesNotCarryTheConsecutiveFastRun) {
  DisplayRefreshPolicy policy;
  policy.seedFastSinceFull(DisplayRefreshPolicy::MAX_FAST_BEFORE_FULL - 20);
  for (uint8_t i = 0; i < DisplayRefreshPolicy::MAX_CONSECUTIVE_FAST; ++i) {
    EXPECT_EQ(policy.choose(Mode::Fast, 1000 + i), Mode::Fast);
  }
  EXPECT_EQ(policy.choose(Mode::Fast, 2000), Mode::Clean);
}

TEST(DisplayRefreshPolicy, SeedingCannotExceedTheFullRefreshBudget) {
  DisplayRefreshPolicy policy;
  policy.seedFastSinceFull(250);
  EXPECT_EQ(policy.fastSinceFull(), DisplayRefreshPolicy::MAX_FAST_BEFORE_FULL);
}

// ── Passes the policy does not drive itself ─────────────────────────────────
//
// Grayscale planes are pushed straight to the driver, so the policy never saw them
// even though they leave charge on the panel like any other pass. Every grayscale
// page therefore aged the panel while the budget stood still.

TEST(DisplayRefreshPolicy, AnExternalPassSpendsTheFastBudget) {
  DisplayRefreshPolicy policy;
  policy.noteExternalFastPass();
  policy.noteExternalFastPass();
  EXPECT_EQ(policy.fastSinceFull(), 2);
}

TEST(DisplayRefreshPolicy, ExternalPassesAloneCanEarnAFullRefresh) {
  DisplayRefreshPolicy policy;
  for (uint8_t i = 0; i < DisplayRefreshPolicy::MAX_FAST_BEFORE_FULL; ++i) policy.noteExternalFastPass();
  EXPECT_EQ(policy.choose(Mode::Fast, 1000), Mode::Full);
}

TEST(DisplayRefreshPolicy, ExternalPassesCountTowardTheCleanCapToo) {
  DisplayRefreshPolicy policy;
  for (uint8_t i = 0; i < DisplayRefreshPolicy::MAX_CONSECUTIVE_FAST; ++i) policy.noteExternalFastPass();
  EXPECT_EQ(policy.choose(Mode::Fast, 1000), Mode::Clean);
}

TEST(DisplayRefreshPolicy, ExternalPassesSaturateInsteadOfWrapping) {
  DisplayRefreshPolicy policy;
  for (int i = 0; i < 400; ++i) policy.noteExternalFastPass();
  EXPECT_EQ(policy.fastSinceFull(), DisplayRefreshPolicy::MAX_FAST_BEFORE_FULL);
  EXPECT_EQ(policy.choose(Mode::Fast, 1000), Mode::Full);
}
