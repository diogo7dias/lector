// Host model of many lock/wake cycles over a large wallpaper folder, driving the real
// pick (sleep_queue::pickNext) and the real render-failure rule. It counts how many
// wakes pay the full folder walk. The device flags that walk when a picked record
// fails to render. Before renderFailureNeedsRebuild, every failure flagged it, and a
// rebuild indexes the same unparseable files again, so the walk came back lap after lap.

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "sleep/SleepIndexReconcilePolicy.h"
#include "sleep/SleepQueuePolicy.h"

namespace {

enum class FileState : uint8_t { Ok, Unparseable, Gone };

struct Folder {
  std::vector<FileState> files;  // by name: "w<i>.bmp"
};

using Rule = bool (*)(bool fileMissing, uint32_t deadSlots);

bool oldRule(bool, uint32_t) { return true; }  // 0.60.0: any failed index pick

struct Device {
  std::vector<uint32_t> records;  // file numbers, one record each
  sleep_queue::QueueState queue;
  uint32_t deadSlots = 0;
  bool needsRebuild = false;
  int walks = 0;
};

// The rebuild walk: every file still on the card is indexed by name, parseable or not.
void rebuild(Device& d, const Folder& f, std::mt19937& rng) {
  d.records.clear();
  for (uint32_t i = 0; i < f.files.size(); ++i) {
    if (f.files[i] != FileState::Gone) d.records.push_back(i);
  }
  sleep_queue::reshuffle(d.queue, d.records.size(), rng(), rng());
  d.deadSlots = 0;
  d.needsRebuild = false;
  ++d.walks;
}

// One lock (pick + render) then one wake (the boot gate).
void lockAndWake(Device& d, const Folder& f, std::mt19937& rng, const Rule rule) {
  const auto nameAt = [&](size_t i) { return std::to_string(d.records[i]); };
  const auto exists = [](const std::string&) { return true; };  // the device does not probe
  const auto counterpart = [](const std::string&) { return std::string(); };
  const auto r = sleep_queue::pickNext(d.queue, d.records.size(), rng(), rng(), nameAt, exists, counterpart);
  if (r.lapWrapped && d.deadSlots > 0) d.needsRebuild = true;  // noteLapWrapped
  if (!r.basename.empty()) {
    const FileState s = f.files[std::stoul(r.basename)];
    if (s != FileState::Ok && rule(s == FileState::Gone, d.deadSlots)) d.needsRebuild = true;
  }
  if (d.needsRebuild) rebuild(d, f, rng);
}

// 6000 wallpapers, 60 that the BMP parser rejects, then 20 on-device deletes and
// pauses (accounted holes). 3000 wakes: half a lap.
int walksOver3000Wakes(const Rule rule, const bool externalDelete) {
  std::mt19937 rng(524);
  Folder f;
  f.files.assign(6000, FileState::Ok);
  for (uint32_t i = 0; i < 6000; i += 100) f.files[i] = FileState::Unparseable;
  Device d;
  rebuild(d, f, rng);
  d.walks = 0;  // the first index build is not a wake's walk
  for (uint32_t i = 1; i < 6000 && d.deadSlots < 20; i += 97) {
    if (f.files[i] == FileState::Ok) {
      f.files[i] = FileState::Gone;
      ++d.deadSlots;
    }
  }
  // A file removed from a computer while the device slept: no hole accounts for it.
  if (externalDelete) {
    d.deadSlots = 0;
    f.files[4242] = FileState::Gone;
    // Make it the next pick so the heal is deterministic.
    d.records.push_back(4242);
    d.queue.freshNext = static_cast<uint32_t>(d.records.size() - 1);
  }
  for (int wake = 0; wake < 3000; ++wake) lockAndWake(d, f, rng, rule);
  return d.walks;
}

TEST(WakeRebuildLoop, UnparseableFilesAndHolesNeverWalkPerWake) {
  const int before = walksOver3000Wakes(oldRule, false);
  const int after = walksOver3000Wakes(sleep_reconcile::renderFailureNeedsRebuild, false);
  std::printf("walks per 3000 wakes: 0.60.0 rule %d, fixed rule %d\n", before, after);
  EXPECT_GT(before, 10);
  EXPECT_EQ(after, 0);
}

TEST(WakeRebuildLoop, AFileGoneBehindTheIndexHealsOnce) {
  EXPECT_EQ(walksOver3000Wakes(sleep_reconcile::renderFailureNeedsRebuild, true), 1);
}

TEST(WakeRebuildLoop, RuleTable) {
  EXPECT_TRUE(sleep_reconcile::renderFailureNeedsRebuild(true, 0));
  EXPECT_FALSE(sleep_reconcile::renderFailureNeedsRebuild(true, 3));
  EXPECT_FALSE(sleep_reconcile::renderFailureNeedsRebuild(false, 0));
}

}  // namespace
