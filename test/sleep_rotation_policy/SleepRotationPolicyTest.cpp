// Pure-math regression for the sleep wallpaper rotation cursor.
//
// The wallpaper lock-screen must show every image once per lap, in a shuffled
// order, then reshuffle for a fresh lap — with no stored permutation and O(1)
// state that survives a deep-sleep reboot. This proves the cursor math in
// sleep_rotation:: independently of SD, the folder index, and the framebuffer.

#include <gtest/gtest.h>

#include <set>
#include <vector>

#include "sleep/SleepRotationPolicy.h"

using sleep_rotation::Cursor;

namespace {

// Walk `count` steps from a freshly-seeded cursor and collect the physical index
// shown at each step. randA/randB are fixed so the test is deterministic.
std::vector<size_t> oneLap(const size_t count, const uint32_t randA, const uint32_t randB) {
  Cursor c;
  sleep_rotation::reseed(c, count, randA, randB);
  std::vector<size_t> shown;
  for (size_t i = 0; i < count; ++i) {
    shown.push_back(sleep_rotation::physicalIndex(c, count));
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
    const size_t p = sleep_rotation::physicalIndex(c, count);
    if (i < count)
      lap.insert(p);
    else
      secondLap.insert(p);
    sleep_rotation::advance(c, count, static_cast<uint32_t>(100 + i), static_cast<uint32_t>(200 + i));
  }
  EXPECT_EQ(lap.size(), count);
  EXPECT_EQ(secondLap.size(), count);
}

// The file count changing mid-lap (upload/delete) reseeds so the shuffle stays a
// TRUE permutation of the new size — an affine map with a multiplier chosen for
// the old count is not coprime with the new count in general, which would repeat
// some slots and skip others.
TEST(SleepRotationPolicy, CountChangeReseedsToValidPermutation) {
  Cursor c;
  sleep_rotation::reseed(c, 10, 5u, 6u);
  for (int i = 0; i < 3; ++i) sleep_rotation::advance(c, 10, 5u, 6u);

  // The folder shrinks to 4: the stale cursor must be flagged, and a reseeded
  // lap over the new count covers every slot exactly once.
  EXPECT_TRUE(sleep_rotation::needsReseed(c, 4));
  const auto lap = oneLap(4, 9u, 13u);
  EXPECT_EQ(std::set<size_t>(lap.begin(), lap.end()).size(), 4u);

  // The folder grows to 360 (bulk upload): same guarantee, full coverage.
  sleep_rotation::reseed(c, 4, 9u, 13u);
  EXPECT_TRUE(sleep_rotation::needsReseed(c, 360));
  const auto bigLap = oneLap(360, 21u, 34u);
  EXPECT_EQ(std::set<size_t>(bigLap.begin(), bigLap.end()).size(), 360u);

  // advance() self-heals a stale cursor in place (no explicit reseed call) and
  // every subsequent pick stays in range for the new count.
  sleep_rotation::reseed(c, 10, 5u, 6u);
  for (int i = 0; i < 20; ++i) {
    sleep_rotation::advance(c, 4, static_cast<uint32_t>(i + 1), static_cast<uint32_t>(i + 2));
    EXPECT_LT(sleep_rotation::physicalIndex(c, 4), 4u);
    EXPECT_FALSE(sleep_rotation::needsReseed(c, 4));
  }
}

// Degenerate sizes never crash or return an out-of-range index.
TEST(SleepRotationPolicy, EmptyAndSingleAreSafe) {
  Cursor c;
  sleep_rotation::reseed(c, 0, 1u, 1u);
  EXPECT_EQ(sleep_rotation::physicalIndex(c, 0), 0u);
  sleep_rotation::advance(c, 0, 1u, 1u);  // must not divide by zero

  sleep_rotation::reseed(c, 1, 1u, 1u);
  EXPECT_EQ(sleep_rotation::physicalIndex(c, 1), 0u);
  sleep_rotation::advance(c, 1, 1u, 1u);
  EXPECT_EQ(sleep_rotation::physicalIndex(c, 1), 0u);
}
