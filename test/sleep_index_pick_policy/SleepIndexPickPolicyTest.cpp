// Pick-policy regression for the sleep wallpaper index engine.
//
// pickNext() must return the next LIVE wallpaper under the shuffled cursor,
// skipping index records whose file vanished, following favorite renames to the
// counterpart name, and flagging a stale index instead of looping forever.

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "sleep/SleepIndexPickPolicy.h"

using sleep_index_pick::pickNext;
using sleep_index_pick::Result;
using sleep_rotation::Cursor;

namespace {

// Fake index + folder: records are the index contents (fixed at build time),
// live is the current /sleep truth (mutated by tests).
struct Fake {
  std::vector<std::string> records;
  std::unordered_set<std::string> live;
  Cursor cursor;

  Result pick(uint32_t randA = 7u, uint32_t randB = 11u) {
    return pickNext(
        cursor, records.size(), randA, randB, [&](size_t i) { return i < records.size() ? records[i] : std::string(); },
        [&](const std::string& n) { return live.count(n) > 0; },
        [](const std::string& n) {
          const auto dot = n.find_last_of('.');
          const std::string stem = dot == std::string::npos ? n : n.substr(0, dot);
          const std::string ext = dot == std::string::npos ? "" : n.substr(dot);
          if (stem.size() >= 2 && stem.compare(stem.size() - 2, 2, "_F") == 0) {
            return stem.substr(0, stem.size() - 2) + ext;
          }
          return stem + "_F" + ext;
        });
  }

  static Fake of(std::vector<std::string> names) {
    Fake f;
    f.records = names;
    f.live.insert(names.begin(), names.end());
    return f;
  }
};

}  // namespace

// A full lap of picks shows every live wallpaper exactly once.
TEST(SleepIndexPickPolicy, LapCoversEveryLiveFileOnce) {
  auto f = Fake::of({"A.pxc", "B.pxc", "C.pxc", "D.pxc", "E.pxc"});
  std::set<std::string> shown;
  for (int i = 0; i < 5; ++i) {
    const Result r = f.pick();
    ASSERT_FALSE(r.basename.empty());
    ASSERT_FALSE(r.needsRebuild);
    shown.insert(r.basename);
  }
  EXPECT_EQ(shown.size(), 5u);
}

// A record whose file vanished (deleted / moved to pause) is skipped, and the
// pick lands on a live successor.
TEST(SleepIndexPickPolicy, SkipsVanishedFiles) {
  auto f = Fake::of({"A.pxc", "B.pxc", "C.pxc", "D.pxc"});
  f.live.erase("B.pxc");
  f.live.erase("C.pxc");
  std::set<std::string> shown;
  for (int i = 0; i < 4; ++i) {
    const Result r = f.pick();
    if (!r.basename.empty()) shown.insert(r.basename);
  }
  EXPECT_EQ(shown, (std::set<std::string>{"A.pxc", "D.pxc"}));
}

// A favorited file (renamed x.pxc -> x_F.pxc after the index was built) keeps
// its rotation slot via the counterpart lookup instead of being skipped.
TEST(SleepIndexPickPolicy, FollowsFavoriteRenameCounterpart) {
  auto f = Fake::of({"A.pxc", "B.pxc", "C.pxc"});
  f.live.erase("B.pxc");
  f.live.insert("B_F.pxc");
  std::set<std::string> shown;
  for (int i = 0; i < 3; ++i) shown.insert(f.pick().basename);
  EXPECT_EQ(shown, (std::set<std::string>{"A.pxc", "B_F.pxc", "C.pxc"}));
}

// Every record dead -> needsRebuild, bounded (no infinite walk), empty name.
TEST(SleepIndexPickPolicy, AllDeadFlagsRebuild) {
  auto f = Fake::of({"A.pxc", "B.pxc", "C.pxc"});
  f.live.clear();
  const Result r = f.pick();
  EXPECT_TRUE(r.needsRebuild);
  EXPECT_TRUE(r.basename.empty());
}

// Empty index -> empty result, no rebuild loop.
TEST(SleepIndexPickPolicy, EmptyIndexReturnsEmpty) {
  Fake f;
  const Result r = f.pick();
  EXPECT_TRUE(r.basename.empty());
  EXPECT_FALSE(r.needsRebuild);
}

// The cursor advances past the returned candidate: two consecutive picks never
// return the same file while more than one is live.
TEST(SleepIndexPickPolicy, ConsecutivePicksDiffer) {
  auto f = Fake::of({"A.pxc", "B.pxc", "C.pxc"});
  std::string prev;
  for (int i = 0; i < 12; ++i) {
    const Result r = f.pick();
    ASSERT_FALSE(r.basename.empty());
    EXPECT_NE(r.basename, prev);
    prev = r.basename;
  }
}

// Huge stale index (5000 records, most dead) stays bounded per pick and still
// finds the live files.
TEST(SleepIndexPickPolicy, BoundedOnHugeStaleIndex) {
  Fake f;
  for (int i = 0; i < 5000; ++i) f.records.push_back("img" + std::to_string(i) + ".pxc");
  // Only a scattering still lives.
  for (int i = 0; i < 5000; i += 500) f.live.insert("img" + std::to_string(i) + ".pxc");
  int found = 0;
  int rebuilds = 0;
  for (int i = 0; i < 40; ++i) {
    const Result r = f.pick(static_cast<uint32_t>(i * 3 + 1), static_cast<uint32_t>(i * 7 + 2));
    if (!r.basename.empty()) ++found;
    if (r.needsRebuild) ++rebuilds;
  }
  EXPECT_GT(found, 0);  // still serves images
  // With 10/5000 alive, some picks legitimately exhaust the 64-skip budget and
  // ask for a rebuild — that is the designed signal, not a failure.
  EXPECT_EQ(found + rebuilds, 40);
}
