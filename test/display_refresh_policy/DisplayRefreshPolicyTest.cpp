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

// ── Ink debt ────────────────────────────────────────────────────────────────
//
// The counters above treat every FAST pass as equal, which is why a session spent in
// menus and covers ghosts while a reading session of the same length stays clean. The
// debt charges each pass what it actually costs the panel (FrameInkMetrics' 0..1000
// scale) and cleans earlier when the content is heavy. It can only ever escalate SOONER:
// every test above still passes unchanged, because they pass no score and a score of zero
// leaves the counters in sole charge.

TEST(DisplayRefreshPolicy, AZeroScoreLeavesTheCountersInSoleCharge) {
  DisplayRefreshPolicy policy;
  for (uint8_t i = 0; i < DisplayRefreshPolicy::MAX_CONSECUTIVE_FAST; ++i) {
    EXPECT_EQ(policy.choose(Mode::Fast, 1000 + i, 0), Mode::Fast);
  }
  EXPECT_EQ(policy.choose(Mode::Fast, 2000, 0), Mode::Clean);
  EXPECT_EQ(policy.inkDebt(), 0);
}

// A page of body text scores about 300. Twelve of them is the cadence the consecutive-FAST
// cap already gave reading, so the debt must not make ordinary reading flash more often
// than it does today.
TEST(DisplayRefreshPolicy, OrdinaryReadingKeepsItsExistingCadence) {
  DisplayRefreshPolicy policy;
  constexpr uint16_t kTextPage = 300;
  for (uint8_t i = 0; i < DisplayRefreshPolicy::MAX_CONSECUTIVE_FAST; ++i) {
    EXPECT_EQ(policy.choose(Mode::Fast, 1000 + i, kTextPage), Mode::Fast) << "page " << static_cast<int>(i);
  }
  EXPECT_EQ(policy.choose(Mode::Fast, 2000, kTextPage), Mode::Clean);
}

// Heavy content is the case the counters miss: a full-screen inversion leaves as much
// residue in four passes as reading does in twelve.
TEST(DisplayRefreshPolicy, HeavyContentEarnsACleanWithinAFewPasses) {
  DisplayRefreshPolicy policy;
  constexpr uint16_t kInversion = 1000;
  int passes = 0;
  while (policy.choose(Mode::Fast, 1000 + passes, kInversion) == Mode::Fast) {
    passes++;
    ASSERT_LT(passes, DisplayRefreshPolicy::MAX_CONSECUTIVE_FAST) << "the debt never fired";
  }
  EXPECT_LE(passes, 4);
}

TEST(DisplayRefreshPolicy, ACleanLeavesSomeDebtBehind) {
  DisplayRefreshPolicy policy;
  constexpr uint16_t kInversion = 1000;
  while (policy.choose(Mode::Fast, 1000, kInversion) == Mode::Fast) {
  }
  // Scrubbed, not discharged: what is left is why a screen that keeps demanding cleans
  // eventually earns a real FULL.
  EXPECT_GT(policy.inkDebt(), 0);
}

TEST(DisplayRefreshPolicy, AFullDischargesTheDebtEntirely) {
  DisplayRefreshPolicy policy;
  policy.choose(Mode::Fast, 1000, 900);
  ASSERT_GT(policy.inkDebt(), 0);
  EXPECT_EQ(policy.choose(Mode::Full, 1001, 0), Mode::Full);
  EXPECT_EQ(policy.inkDebt(), 0);
}

TEST(DisplayRefreshPolicy, RelentlesslyHeavyContentEventuallyEarnsAFull) {
  DisplayRefreshPolicy policy;
  bool sawFull = false;
  for (int i = 0; i < 200 && !sawFull; ++i) {
    sawFull = policy.choose(Mode::Fast, 1000 + i, 1000) == Mode::Full;
  }
  EXPECT_TRUE(sawFull);
}

// The debt must never be able to DELAY an escalation the counters would have made, or the
// change would be capable of making ghosting worse than it is today.
TEST(DisplayRefreshPolicy, TheDebtCannotDelayTheCountersEscalation) {
  DisplayRefreshPolicy policy;
  policy.seedFastSinceFull(DisplayRefreshPolicy::MAX_FAST_BEFORE_FULL - 1);
  EXPECT_EQ(policy.choose(Mode::Fast, 1000, 1), Mode::Fast);
  // Scored as nearly free, and promoted anyway: the ceiling is the ceiling.
  EXPECT_EQ(policy.choose(Mode::Fast, 1001, 1), Mode::Full);
}

TEST(DisplayRefreshPolicy, DebtSurvivesALock) {
  DisplayRefreshPolicy before;
  before.choose(Mode::Fast, 1000, 900);
  before.choose(Mode::Fast, 1001, 900);
  ASSERT_EQ(before.inkDebt(), 1800);

  DisplayRefreshPolicy after;
  after.seedInkDebt(before.inkDebt());
  EXPECT_EQ(after.inkDebt(), 1800);
}

TEST(DisplayRefreshPolicy, SeedingCannotExceedTheDebtCeiling) {
  DisplayRefreshPolicy policy;
  policy.seedInkDebt(60000);
  EXPECT_EQ(policy.inkDebt(), DisplayRefreshPolicy::DEBT_FULL_THRESHOLD);
}

// Grayscale planes never reach choose(), so they were invisible to the debt as well as to
// the counters. They drive the panel with one-frame phases and are charged accordingly.
TEST(DisplayRefreshPolicy, AnExternalPassChargesDebtToo) {
  DisplayRefreshPolicy policy;
  policy.noteExternalFastPass();
  EXPECT_EQ(policy.inkDebt(), DisplayRefreshPolicy::EXTERNAL_PASS_SCORE);
}

TEST(DisplayRefreshPolicy, DebtSaturatesInsteadOfWrapping) {
  DisplayRefreshPolicy policy;
  for (int i = 0; i < 500; ++i) policy.noteExternalFastPass(1000);
  EXPECT_EQ(policy.inkDebt(), DisplayRefreshPolicy::DEBT_FULL_THRESHOLD);
}

// ── Fast Turbo ───────────────────────────────────────────────────────────────
// The cheap partial path skips the vendor sequence's temperature reload, so the panel
// is handed back to the standard sequence periodically to re-read its own temperature.

TEST(DisplayRefreshPolicy, TurboIsRefusedWhenNobodyAsksForIt) {
  DisplayRefreshPolicy policy;
  for (int i = 0; i < 50; ++i) EXPECT_FALSE(policy.useTurbo(false));
}

TEST(DisplayRefreshPolicy, EveryNthTurboPassRunsTheStandardSequence) {
  DisplayRefreshPolicy policy;
  int standard = 0;
  for (int i = 0; i < DisplayRefreshPolicy::TURBO_RELOAD_EVERY * 4; ++i) {
    if (!policy.useTurbo(true)) ++standard;
  }
  EXPECT_EQ(standard, 4);
}

// A screen that never asks must not advance the cadence, or the reload would land on an
// arbitrary pass instead of every Nth cheap one.
TEST(DisplayRefreshPolicy, RefusedPassesDoNotAdvanceTheReloadCadence) {
  DisplayRefreshPolicy policy;
  for (int i = 0; i < DisplayRefreshPolicy::TURBO_RELOAD_EVERY - 1; ++i) ASSERT_TRUE(policy.useTurbo(true));
  for (int i = 0; i < 20; ++i) ASSERT_FALSE(policy.useTurbo(false));
  EXPECT_FALSE(policy.useTurbo(true));  // still the Nth cheap pass, not the 20th call
}

TEST(DisplayRefreshPolicy, ATurboPassChargesMoreDebtThanTheSamePassWouldOtherwise) {
  DisplayRefreshPolicy standard;
  standard.choose(Mode::Fast, 1000, 300, /*turboPass=*/false);

  DisplayRefreshPolicy turbo;
  turbo.choose(Mode::Fast, 1000, 300, /*turboPass=*/true);

  EXPECT_EQ(turbo.inkDebt(), standard.inkDebt() * DisplayRefreshPolicy::TURBO_DEBT_MULTIPLIER);
}

// Charging more must bring the clean FORWARD, which is the whole point of charging more.
TEST(DisplayRefreshPolicy, TurboReachesACleanSoonerThanStandardDoes) {
  auto passesUntilClean = [](const bool turbo) {
    DisplayRefreshPolicy policy;
    for (int i = 0; i < 100; ++i) {
      if (policy.choose(Mode::Fast, 1000 + i, 300, turbo) != Mode::Fast) return i;
    }
    return 100;
  };
  EXPECT_LT(passesUntilClean(true), passesUntilClean(false));
}

// The multiplied score is still one pass, so it cannot exceed what the heaviest possible
// single frame costs. Otherwise a turbo pass could outrun the whole-frame-inversion case
// the thresholds were set against.
TEST(DisplayRefreshPolicy, AMultipliedScoreStillCannotExceedOneWholeFrame) {
  DisplayRefreshPolicy policy;
  policy.choose(Mode::Fast, 1000, 1000, /*turboPass=*/true);
  EXPECT_EQ(policy.inkDebt(), 1000);
}

TEST(DisplayRefreshPolicy, ResetClearsTheReloadCadence) {
  DisplayRefreshPolicy policy;
  for (int i = 0; i < DisplayRefreshPolicy::TURBO_RELOAD_EVERY - 1; ++i) ASSERT_TRUE(policy.useTurbo(true));
  policy.reset();
  EXPECT_TRUE(policy.useTurbo(true));
}
