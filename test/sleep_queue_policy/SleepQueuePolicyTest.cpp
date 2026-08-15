// Host proof of the sleep wallpaper queue: fresh (appended) records serve
// first, then the shuffled lap; laps never repeat a wallpaper; dead slots are
// skipped and consumed; lap completion and manual reshuffle fold the fresh
// region into a new lap over everything.

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sleep/SleepQueuePolicy.h"

using sleep_queue::QueueState;
using sleep_queue::Result;

namespace {

// In-memory stand-in for the index file + folder. Records are append-only
// (like the real file); the live set can diverge (deletes, favorite renames).
struct Fake {
  std::vector<std::string> records;
  std::set<std::string> live;

  void addLive(const std::string& name) {
    records.push_back(name);
    live.insert(name);
  }

  Result pick(QueueState& s, const uint32_t randA = 7, const uint32_t randB = 13) {
    return sleep_queue::pickNext(
        s, records.size(), randA, randB,
        [&](const size_t i) { return i < records.size() ? records[i] : std::string(); },
        [&](const std::string& n) { return live.count(n) > 0; },
        [](const std::string& n) {
          // Favorite-rename counterpart: flip a "_F" before the ".pxc".
          const std::string ext = ".pxc";
          if (n.size() < ext.size()) return std::string();
          const std::string stem = n.substr(0, n.size() - ext.size());
          if (stem.size() >= 2 && stem.substr(stem.size() - 2) == "_F") return stem.substr(0, stem.size() - 2) + ext;
          return stem + "_F" + ext;
        });
  }
};

Fake makeFake(const size_t count, const std::string& prefix = "w") {
  Fake f;
  for (size_t i = 0; i < count; ++i) f.addLive(prefix + std::to_string(i) + ".pxc");
  return f;
}

// Seed a lap over everything currently in the fake (what a rebuild does).
QueueState seeded(const Fake& f, const uint32_t randA = 3, const uint32_t randB = 5) {
  QueueState s;
  sleep_queue::reshuffle(s, f.records.size(), randA, randB);
  return s;
}

}  // namespace

// One lap shows every live wallpaper exactly once.
TEST(SleepQueuePolicy, LapShowsEveryWallpaperOnce) {
  auto f = makeFake(37);
  auto s = seeded(f);
  std::set<std::string> shown;
  for (size_t i = 0; i < f.records.size(); ++i) {
    const auto r = f.pick(s);
    ASSERT_FALSE(r.basename.empty());
    EXPECT_TRUE(shown.insert(r.basename).second) << "repeat within a lap: " << r.basename;
  }
  EXPECT_EQ(shown.size(), f.records.size());
}

// Appended records jump the queue: they serve next, in append order, exactly
// once, and then the old lap resumes where it left off.
TEST(SleepQueuePolicy, FreshRecordsServeFirstInOrder) {
  auto f = makeFake(10);
  auto s = seeded(f);

  // Half a lap...
  std::set<std::string> before;
  for (int i = 0; i < 5; ++i) before.insert(f.pick(s).basename);

  // ...then 3 files land (cold-boot append: records grow, freshNext lags).
  f.addLive("new0.pxc");
  f.addLive("new1.pxc");
  f.addLive("new2.pxc");

  EXPECT_EQ(f.pick(s).basename, "new0.pxc");
  EXPECT_EQ(f.pick(s).basename, "new1.pxc");
  EXPECT_EQ(f.pick(s).basename, "new2.pxc");

  // The remaining 5 picks of the lap are the 5 old wallpapers not yet shown —
  // fresh files did not displace anyone.
  std::set<std::string> after;
  for (int i = 0; i < 5; ++i) after.insert(f.pick(s).basename);
  EXPECT_EQ(after.size(), 5u);
  for (const auto& n : after) {
    EXPECT_EQ(before.count(n), 0u) << "lap repeated " << n;
    EXPECT_EQ(n[0], 'w') << "fresh name resumed inside the old lap: " << n;
  }
}

// Lap completion folds fresh records into a reshuffled lap over everything.
TEST(SleepQueuePolicy, LapWrapReshufflesOverEverything) {
  auto f = makeFake(6);
  auto s = seeded(f);
  for (int i = 0; i < 3; ++i) f.pick(s);
  f.addLive("late0.pxc");
  f.addLive("late1.pxc");
  // Drain: 2 fresh + remaining 3 of the lap.
  for (int i = 0; i < 5; ++i) ASSERT_FALSE(f.pick(s).basename.empty());

  // Next lap covers ALL 8, each exactly once.
  std::set<std::string> lap;
  for (size_t i = 0; i < f.records.size(); ++i) {
    const auto r = f.pick(s, static_cast<uint32_t>(i + 21), static_cast<uint32_t>(i + 42));
    EXPECT_TRUE(lap.insert(r.basename).second) << "repeat: " << r.basename;
  }
  EXPECT_EQ(lap.size(), 8u);
}

// Manual reshuffle folds a pending fresh region into the new lap: nothing is
// lost, nothing repeats within the lap.
TEST(SleepQueuePolicy, ManualReshuffleFoldsFreshQueue) {
  auto f = makeFake(5);
  auto s = seeded(f);
  f.pick(s);
  f.addLive("fresh.pxc");

  sleep_queue::reshuffle(s, f.records.size(), 77u, 88u);
  EXPECT_EQ(s.freshNext, f.records.size());

  std::set<std::string> lap;
  for (size_t i = 0; i < f.records.size(); ++i) lap.insert(f.pick(s).basename);
  EXPECT_EQ(lap.size(), 6u);
  EXPECT_EQ(lap.count("fresh.pxc"), 1u);
}

// Dead slots (deleted files) are skipped without ending the lap, and every
// remaining live wallpaper still shows.
TEST(SleepQueuePolicy, DeadSlotsAreSkipped) {
  auto f = makeFake(20);
  auto s = seeded(f);
  // Delete 6 files behind the index's back.
  for (int i = 2; i < 20; i += 3) f.live.erase("w" + std::to_string(i) + ".pxc");

  std::set<std::string> shown;
  // 14 live files; every pick must land on a live one.
  for (int i = 0; i < 14; ++i) {
    const auto r = f.pick(s, static_cast<uint32_t>(i + 5), static_cast<uint32_t>(i + 6));
    ASSERT_FALSE(r.basename.empty());
    EXPECT_TRUE(f.live.count(r.basename)) << r.basename;
    shown.insert(r.basename);
  }
  EXPECT_EQ(shown.size(), f.live.size());
}

// A favorite rename keeps its rotation slot: the record holds the old name,
// the counterpart resolves to the renamed file, and no slot is wasted.
TEST(SleepQueuePolicy, FavoriteRenameKeepsItsSlot) {
  auto f = makeFake(4);
  auto s = seeded(f);
  // w1.pxc got favorited on the card: the file is now w1_F.pxc.
  f.live.erase("w1.pxc");
  f.live.insert("w1_F.pxc");

  std::set<std::string> shown;
  for (int i = 0; i < 4; ++i) shown.insert(f.pick(s).basename);
  EXPECT_EQ(shown.count("w1_F.pxc"), 1u);
  EXPECT_EQ(shown.count("w1.pxc"), 0u);
  EXPECT_EQ(shown.size(), 4u);
}

// Dead fresh slots are consumed, not retried: a fresh file deleted before it
// ever showed just vanishes from the queue.
TEST(SleepQueuePolicy, DeadFreshSlotsAreConsumed) {
  auto f = makeFake(3);
  auto s = seeded(f);
  f.addLive("a.pxc");
  f.addLive("b.pxc");
  f.live.erase("a.pxc");  // gone before it showed

  EXPECT_EQ(f.pick(s).basename, "b.pxc");
  EXPECT_EQ(s.freshNext, f.records.size());
}

// When every inspected slot is dead the pick gives up and asks for a rebuild
// instead of walking a 20000-record graveyard on the sleep path.
TEST(SleepQueuePolicy, ExhaustedSkipBudgetRequestsRebuild) {
  auto f = makeFake(200);
  auto s = seeded(f);
  f.live.clear();  // everything deleted behind the index's back
  const auto r = f.pick(s);
  EXPECT_TRUE(r.basename.empty());
  EXPECT_TRUE(r.needsRebuild);
}

// Crash-replay convention: state persisted BEFORE the render is re-walked from
// the same spot after a crash, and the pick still works.
TEST(SleepQueuePolicy, PersistedStateReplaysCleanly) {
  auto f = makeFake(12);
  auto s = seeded(f);
  for (int i = 0; i < 4; ++i) f.pick(s);

  const QueueState snapshot = s;  // "state.json wrote here"
  const auto picked = f.pick(s).basename;

  QueueState replay = snapshot;  // "crashed before render, rebooted"
  EXPECT_EQ(f.pick(replay).basename, picked);
}

// Whole-system property: random deletes and appends over many picks, and still
// no lap ever repeats a wallpaper it already showed in that lap.
TEST(SleepQueuePolicy, PropertyNoIntraLapRepeats) {
  auto f = makeFake(30);
  auto s = seeded(f);
  uint32_t rng = 12345;
  const auto nextRand = [&rng] {
    rng = rng * 1103515245u + 12345u;
    return rng;
  };

  // Tracks one uninterrupted lap segment. The pick that CROSSES a reseed
  // boundary is ambiguous from outside (it may be the old lap's last item or
  // the new lap's first), so a boundary resets the window instead of judging
  // that pick — every pick strictly inside a segment must still be unique.
  std::set<std::string> segmentShown;
  for (int step = 0; step < 500; ++step) {
    // Occasionally mutate the world like a cold boot would.
    if (step % 37 == 21) f.addLive("x" + std::to_string(step) + ".pxc");
    if (step % 53 == 13 && !f.live.empty()) f.live.erase(*f.live.begin());

    const size_t seededBefore = s.cursor.seededCount;
    const uint32_t posBefore = s.cursor.position;
    const auto r = f.pick(s, nextRand(), nextRand());
    if (r.needsRebuild) break;  // world got too stale; the device would rebuild
    ASSERT_FALSE(r.basename.empty());
    EXPECT_TRUE(f.live.count(r.basename));

    const bool crossedBoundary = s.cursor.seededCount != seededBefore || s.cursor.position <= posBefore;
    if (crossedBoundary) {
      segmentShown.clear();
      continue;
    }
    EXPECT_TRUE(segmentShown.insert(r.basename).second)
        << "repeat within one lap segment at step " << step << " (seeded " << s.cursor.seededCount << ")";
  }
}

// The pick reports the lap wrap so the device can defer index compaction to the
// end of a lap: holes left by deletes stay cheap until every wallpaper in the
// lap has been shown once, and only then is a rebuild worth its scan.
TEST(SleepQueuePolicy, ReportsTheLapWrap) {
  Fake fake;
  for (int i = 0; i < 4; ++i) fake.addLive("w" + std::to_string(i) + ".pxc");

  QueueState s;
  sleep_queue::reshuffle(s, fake.records.size(), 7, 13);

  // One full lap: exactly the last pick of the lap reports the wrap.
  for (size_t i = 0; i < fake.records.size(); ++i) {
    const Result r = fake.pick(s);
    ASSERT_FALSE(r.basename.empty());
    const bool isLastOfLap = i + 1 == fake.records.size();
    EXPECT_EQ(r.lapWrapped, isLastOfLap) << "pick " << i;
  }
}

// A pick served from the fresh region is not a lap wrap.
TEST(SleepQueuePolicy, FreshPickDoesNotReportALapWrap) {
  Fake fake;
  for (int i = 0; i < 3; ++i) fake.addLive("w" + std::to_string(i) + ".pxc");
  QueueState s;
  sleep_queue::reshuffle(s, fake.records.size(), 7, 13);
  fake.addLive("fresh.pxc");  // appended after the lap was seeded

  const Result r = fake.pick(s);
  EXPECT_EQ(r.basename, "fresh.pxc");
  EXPECT_FALSE(r.lapWrapped);
}
