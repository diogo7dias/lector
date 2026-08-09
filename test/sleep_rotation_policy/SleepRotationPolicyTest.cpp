// Pure-math regression for the sleep wallpaper rotation cursor.
//
// The wallpaper lock-screen must show every image once per lap, in a shuffled
// order, then reshuffle for a fresh lap — with no stored permutation and O(1)
// state that survives a deep-sleep reboot. This proves the cursor math in
// sleep_rotation:: independently of SD, the index file, and the framebuffer.
//
// Adapted from the pre-rebase suite (git f1368a76 / ffa834bd): advance() now
// takes the lap domain from the cursor itself and a separate reseed count, so
// records appended mid-lap never restart the lap in flight.

#include <gtest/gtest.h>

#include <set>
#include <vector>

#include "sleep/SleepRotationPolicy.h"

using sleep_rotation::Cursor;

namespace {

// Walk one full lap from a freshly-seeded cursor and collect the physical index
// shown at each step. randA/randB are fixed so the test is deterministic.
std::vector<size_t> oneLap(const size_t count, const uint32_t randA, const uint32_t randB) {
  Cursor c;
  sleep_rotation::reseed(c, count, randA, randB);
  std::vector<size_t> shown;
  for (size_t i = 0; i < count; ++i) {
    shown.push_back(sleep_rotation::physicalIndex(c));
    sleep_rotation::advance(c, count, randA, randB);
  }
  return shown;
}

}  // namespace

// A single lap visits every physical slot exactly once (it is a permutation).
TEST(SleepRotationPolicy, LapIsAPermutationOverAllFiles) {
  for (size_t count : {2u, 3u, 5u, 7u, 360u, 5000u}) {
    const auto shown = oneLap(count, 12345u, 67890u);
    ASSERT_EQ(shown.size(), count);
    std::set<size_t> distinct(shown.begin(), shown.end());
    EXPECT_EQ(distinct.size(), count) << "count=" << count;
    for (size_t v : shown) EXPECT_LT(v, count) << "count=" << count;
  }
}

// Different lap seeds produce different orders (real shuffle, not a fixed walk).
TEST(SleepRotationPolicy, DifferentSeedsGiveDifferentOrders) {
  const auto a = oneLap(360u, 11u, 22u);
  const auto b = oneLap(360u, 99u, 7u);
  EXPECT_NE(a, b);
  // Both are still complete permutations.
  EXPECT_EQ(std::set<size_t>(a.begin(), a.end()).size(), 360u);
  EXPECT_EQ(std::set<size_t>(b.begin(), b.end()).size(), 360u);
}

// Crossing a lap boundary reseeds and the next lap is again a full permutation,
// simulating many deep-sleep reboots back to back.
TEST(SleepRotationPolicy, ReseedsAtLapBoundaryAndKeepsCovering) {
  const size_t count = 50;
  Cursor c;
  sleep_rotation::reseed(c, count, 3u, 4u);
  std::set<size_t> lap;
  std::set<size_t> secondLap;
  for (size_t i = 0; i < count * 2; ++i) {
    const size_t p = sleep_rotation::physicalIndex(c);
    if (i < count)
      lap.insert(p);
    else
      secondLap.insert(p);
    sleep_rotation::advance(c, count, static_cast<uint32_t>(100 + i), static_cast<uint32_t>(200 + i));
  }
  EXPECT_EQ(lap.size(), count);
  EXPECT_EQ(secondLap.size(), count);
}

// THE two-count contract: the lap in flight walks its seeded domain to the end,
// and the reseed at the wrap runs over the CURRENT record count — that is how
// appended records join the rotation without restarting the lap.
TEST(SleepRotationPolicy, WrapReseedsOverReseedCountNotLapCount) {
  const size_t seeded = 10;
  const size_t grown = 14;  // 4 records appended mid-lap
  Cursor c;
  sleep_rotation::reseed(c, seeded, 5u, 6u);

  // Walking the lap never leaves the seeded domain, however large the live
  // count already is.
  bool wrapped = false;
  for (size_t i = 0; i < seeded; ++i) {
    EXPECT_LT(sleep_rotation::physicalIndex(c), seeded);
    wrapped = sleep_rotation::advance(c, grown, static_cast<uint32_t>(i + 3), static_cast<uint32_t>(i + 8));
    if (i + 1 < seeded) {
      EXPECT_FALSE(wrapped) << "wrapped early at step " << i;
    }
  }
  EXPECT_TRUE(wrapped);

  // The wrap reseeded over the grown count: the next lap covers all 14.
  EXPECT_EQ(c.seededCount, grown);
  EXPECT_EQ(c.position, 0u);
  std::set<size_t> nextLap;
  for (size_t i = 0; i < grown; ++i) {
    nextLap.insert(sleep_rotation::physicalIndex(c));
    sleep_rotation::advance(c, grown, 7u, 9u);
  }
  EXPECT_EQ(nextLap.size(), grown);
}

// needsReseed flags only genuinely unusable cursors. A live count LARGER than
// the seeded domain is fine (appends are handled by the fresh queue); a live
// count smaller than the domain is not (only a rebuild shrinks the index).
TEST(SleepRotationPolicy, NeedsReseedSemantics) {
  Cursor fresh;
  EXPECT_TRUE(sleep_rotation::needsReseed(fresh, 10));

  Cursor c;
  sleep_rotation::reseed(c, 10, 5u, 6u);
  EXPECT_FALSE(sleep_rotation::needsReseed(c, 10));
  EXPECT_FALSE(sleep_rotation::needsReseed(c, 14));  // grown: still walkable
  EXPECT_TRUE(sleep_rotation::needsReseed(c, 4));    // shrunk: domain invalid

  c.multiplier = 0;  // corrupt state
  EXPECT_TRUE(sleep_rotation::needsReseed(c, 10));
}

// Degenerate sizes never crash or return an out-of-range index.
TEST(SleepRotationPolicy, EmptyAndSingleAreSafe) {
  Cursor c;
  sleep_rotation::reseed(c, 0, 1u, 1u);
  EXPECT_EQ(sleep_rotation::physicalIndex(c), 0u);
  sleep_rotation::advance(c, 0, 1u, 1u);  // must not divide by zero

  sleep_rotation::reseed(c, 1, 1u, 1u);
  EXPECT_EQ(sleep_rotation::physicalIndex(c), 0u);
  sleep_rotation::advance(c, 1, 1u, 1u);
  EXPECT_EQ(sleep_rotation::physicalIndex(c), 0u);
}
