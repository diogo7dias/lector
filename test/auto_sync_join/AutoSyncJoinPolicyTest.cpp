#include <gtest/gtest.h>

#include "src/util/AutoSyncJoinPolicy.h"

using namespace auto_sync_join;

TEST(AutoSyncJoinPolicy, BailsWhenNoSavedNetworkAppearsInScan) {
  const std::vector<SavedNetwork> saved = {{"home", false}, {"office", false}, {"cafe", false}};
  const std::vector<ScannedNetwork> scanned = {{"airport-free", -45}, {"hotel-guest", -60}};

  const auto candidates = selectCandidates(saved, scanned, "home");
  EXPECT_TRUE(candidates.empty());
}

TEST(AutoSyncJoinPolicy, OrdersLastConnectedThenStrongestSeenAndCapsAtMaxJoins) {
  const std::vector<SavedNetwork> saved = {{"netA", false}, {"netB", false}, {"netC", false}, {"netD", false}};
  const std::vector<ScannedNetwork> scanned = {
      {"netA", -70}, {"netB", -40}, {"netC", -55}, {"netD", -85}, {"netB", -65}};

  const auto candidates = selectCandidates(saved, scanned, "netD", 3);
  ASSERT_EQ(candidates.size(), 3u);
  EXPECT_EQ(candidates[0], "netD");  // last connected wins despite weaker RSSI
  EXPECT_EQ(candidates[1], "netB");  // strongest sighting (-40) next
  EXPECT_EQ(candidates[2], "netC");  // -55 beats -70
}

TEST(AutoSyncJoinPolicy, DirectJoinsOnlyConfiguredHiddenSsids) {
  const std::vector<SavedNetwork> saved = {{"home", false}, {"secret-ap", true}, {"office", false}};
  const std::vector<ScannedNetwork> scanned = {{"stranger", -50}};

  const auto candidates = selectCandidates(saved, scanned, "home");
  ASSERT_EQ(candidates.size(), 1u);
  EXPECT_EQ(candidates[0], "secret-ap");
}

TEST(AutoSyncJoinPolicy, SeenSavedNetworksTakePrecedenceOverHiddenFallback) {
  const std::vector<SavedNetwork> saved = {{"home", false}, {"secret-ap", true}};
  const std::vector<ScannedNetwork> scanned = {{"home", -50}};

  const auto candidates = selectCandidates(saved, scanned, "secret-ap");
  ASSERT_EQ(candidates.size(), 2u);
  EXPECT_EQ(candidates[0], "home");
  EXPECT_EQ(candidates[1], "secret-ap");
}

TEST(AutoSyncJoinPolicy, ExponentialWallClockBackoffAndSuccessReset) {
  BackoffState state{};
  state.magic = 0xDEADBEEFu;  // uninitialized RTC garbage
  constexpr int64_t t0 = 1790000000;

  EXPECT_FALSE(shouldSkip(state, t0));

  recordFailure(state, t0);
  EXPECT_EQ(state.consecutiveFailures, 1);
  EXPECT_TRUE(shouldSkip(state, t0 + BASE_COOLDOWN_S - 1));
  EXPECT_FALSE(shouldSkip(state, t0 + BASE_COOLDOWN_S));

  // Second failure doubles cooldown (10 min).
  const int64_t t1 = t0 + BASE_COOLDOWN_S;
  recordFailure(state, t1);
  EXPECT_EQ(state.consecutiveFailures, 2);
  EXPECT_TRUE(shouldSkip(state, t1 + BASE_COOLDOWN_S * 2 - 1));
  EXPECT_FALSE(shouldSkip(state, t1 + BASE_COOLDOWN_S * 2));

  // Repeated failures cap at MAX_COOLDOWN_S (60 min).
  for (int i = 0; i < 10; ++i) recordFailure(state, t1);
  EXPECT_EQ(state.nextRetryEpoch, t1 + MAX_COOLDOWN_S);

  // Backward clock jump must not trap auto-sync in cooldown.
  EXPECT_FALSE(shouldSkip(state, t1 - 60));

  // Success clears backoff completely.
  recordSuccess(state);
  EXPECT_EQ(state.consecutiveFailures, 0);
  EXPECT_FALSE(shouldSkip(state, t1));
}

TEST(AutoSyncJoinPolicy, UnsetClockFallsBackToBoundedLockSkips) {
  BackoffState state{};
  state.magic = 0;

  recordFailure(state, 0);
  EXPECT_TRUE(shouldSkip(state, 0));
  consumeSkippedLock(state, 0);
  EXPECT_FALSE(shouldSkip(state, 0));

  recordFailure(state, 0);
  EXPECT_EQ(state.skipsRemaining, 2);
  consumeSkippedLock(state, 0);
  EXPECT_TRUE(shouldSkip(state, 0));
  consumeSkippedLock(state, 0);
  EXPECT_FALSE(shouldSkip(state, 0));
}
