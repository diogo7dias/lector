// Host tests for the bulk wallpaper mover.
//
// The mover runs against a folder that can hold thousands of images on a device
// with a fragmented heap, so the batching is not an optimisation — it is the
// whole design. These tests pin the two properties that matter: it never holds
// more than one batch of names at a time, and it never spins forever on files it
// cannot move.
#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "sleep/SleepImageMove.h"

using crosspoint::sleep::countImagesByFavorite;
using crosspoint::sleep::ISleepImageFs;
using crosspoint::sleep::moveImagesByFavorite;
using crosspoint::sleep::NameSink;

namespace {

constexpr const char* kFrom = "/sleep";
constexpr const char* kTo = "/sleep pause";

// In-memory stand-in for the SD card. Counts folder scans, which is how the
// batching shows up from the outside: one scan per batch instead of one scan for
// the whole folder.
class FakeFs final : public ISleepImageFs {
 public:
  std::set<std::string> paths;      // full paths of every file present
  std::set<std::string> madeDirs;   // directories created
  std::set<std::string> unmovable;  // basenames whose rename always fails
  size_t walkCount = 0;             // how many folder scans the run cost

  void walk(const char* dir, const NameSink& sink) override {
    walkCount++;
    const std::string prefix = std::string(dir) + "/";
    // Copy first: the real SD walk hands out names from its own buffer and the
    // caller may not mutate the folder mid-walk, so the fake must not either.
    std::vector<std::string> names;
    for (const auto& path : paths) {
      if (path.rfind(prefix, 0) == 0 && path.find('/', prefix.size()) == std::string::npos) {
        names.push_back(path.substr(prefix.size()));
      }
    }
    for (const auto& name : names) sink(name.c_str(), name.size());
  }

  bool mkdir(const char* path) override {
    madeDirs.insert(path);
    return true;
  }

  bool rename(const char* from, const char* to) override {
    const std::string fromStr(from);
    const std::string base = fromStr.substr(fromStr.find_last_of('/') + 1);
    if (unmovable.count(base) != 0) return false;
    if (paths.count(fromStr) == 0) return false;
    if (paths.count(to) != 0) return false;  // SdFat never overwrites
    paths.erase(fromStr);
    paths.insert(to);
    return true;
  }

  size_t countIn(const std::string& dir) const {
    const std::string prefix = dir + "/";
    size_t n = 0;
    for (const auto& path : paths) {
      if (path.rfind(prefix, 0) == 0) n++;
    }
    return n;
  }
};

FakeFs makeFolder(size_t favorites, size_t others) {
  FakeFs fs;
  for (size_t i = 0; i < favorites; i++) fs.paths.insert("/sleep/fav" + std::to_string(i) + "_F.bmp");
  for (size_t i = 0; i < others; i++) fs.paths.insert("/sleep/plain" + std::to_string(i) + ".bmp");
  return fs;
}

}  // namespace

TEST(CountImagesByFavorite, CountsEachGroup) {
  FakeFs fs = makeFolder(3, 5);
  EXPECT_EQ(3u, countImagesByFavorite(fs, kFrom, true, 1000));
  EXPECT_EQ(5u, countImagesByFavorite(fs, kFrom, false, 1000));
}

TEST(CountImagesByFavorite, StopsAtScanCap) {
  FakeFs fs = makeFolder(50, 0);
  // The cap bounds the confirmation prompt's number; it must not keep counting.
  EXPECT_EQ(10u, countImagesByFavorite(fs, kFrom, true, 10));
}

TEST(CountImagesByFavorite, EmptyFolderCountsZero) {
  FakeFs fs;
  EXPECT_EQ(0u, countImagesByFavorite(fs, kFrom, true, 1000));
}

TEST(MoveImagesByFavorite, MovesOnlyTheRequestedGroup) {
  FakeFs fs = makeFolder(3, 4);
  const auto report = moveImagesByFavorite(fs, kFrom, kTo, true, 8, 0, nullptr);

  EXPECT_EQ(3u, report.moved);
  EXPECT_EQ(0u, report.failed);
  EXPECT_FALSE(report.stalled);
  EXPECT_EQ(4u, fs.countIn(kFrom));
  EXPECT_EQ(3u, fs.countIn(kTo));
  EXPECT_EQ(1u, fs.madeDirs.count(kTo));
}

TEST(MoveImagesByFavorite, MovesNonFavoritesWhenAsked) {
  FakeFs fs = makeFolder(3, 4);
  const auto report = moveImagesByFavorite(fs, kFrom, kTo, false, 8, 0, nullptr);

  EXPECT_EQ(4u, report.moved);
  EXPECT_EQ(3u, fs.countIn(kFrom));
}

TEST(MoveImagesByFavorite, MovesBackTheOtherWay) {
  FakeFs fs;
  fs.paths.insert("/sleep pause/a_F.bmp");
  fs.paths.insert("/sleep pause/b.bmp");

  const auto report = moveImagesByFavorite(fs, kTo, kFrom, true, 8, 0, nullptr);

  EXPECT_EQ(1u, report.moved);
  EXPECT_EQ(1u, fs.paths.count("/sleep/a_F.bmp"));
  EXPECT_EQ(1u, fs.paths.count("/sleep pause/b.bmp"));
}

TEST(MoveImagesByFavorite, HoldsAtMostOneBatchOfNames) {
  // The point of the batching: a 200-image folder must never cost 200 names of
  // heap at once. The mover retains at most batchSize per pass, which shows up
  // as one folder scan per batch instead of a single scan for the whole folder.
  FakeFs fs = makeFolder(200, 0);
  const size_t batchSize = 16;
  const auto report = moveImagesByFavorite(fs, kFrom, kTo, true, batchSize, 0, nullptr);

  EXPECT_EQ(200u, report.moved);
  EXPECT_EQ(0u, fs.countIn(kFrom));
  // ceil(200/16) passes that move a batch, plus the final walk that finds
  // nothing left and ends the run.
  const size_t expectedPasses = (200u + batchSize - 1u) / batchSize;
  EXPECT_EQ(expectedPasses + 1u, fs.walkCount);
}

TEST(MoveImagesByFavorite, EmptyFolderMakesNoDestinationDirectory) {
  FakeFs fs = makeFolder(0, 3);
  const auto report = moveImagesByFavorite(fs, kFrom, kTo, true, 8, 0, nullptr);

  EXPECT_EQ(0u, report.moved);
  EXPECT_FALSE(report.stalled);
  // Nothing matched, so the run must not leave an empty folder behind.
  EXPECT_EQ(0u, fs.madeDirs.count(kTo));
}

TEST(MoveImagesByFavorite, StopsInsteadOfSpinningOnAnUnmovableFile) {
  // A name collision in the destination makes a rename fail forever. Without the
  // stall guard, that file is re-selected by every pass and the loop never ends.
  FakeFs fs = makeFolder(2, 0);
  fs.unmovable.insert("fav0_F.bmp");
  fs.unmovable.insert("fav1_F.bmp");

  const auto report = moveImagesByFavorite(fs, kFrom, kTo, true, 8, 0, nullptr);

  EXPECT_EQ(0u, report.moved);
  EXPECT_EQ(2u, report.failed);
  EXPECT_TRUE(report.stalled);
  EXPECT_EQ(2u, fs.countIn(kFrom));
}

TEST(MoveImagesByFavorite, MovesWhatItCanBeforeStalling) {
  FakeFs fs = makeFolder(5, 0);
  fs.unmovable.insert("fav2_F.bmp");

  const auto report = moveImagesByFavorite(fs, kFrom, kTo, true, 8, 0, nullptr);

  EXPECT_EQ(4u, report.moved);
  EXPECT_EQ(1u, report.failed);
  EXPECT_TRUE(report.stalled);
  EXPECT_EQ(1u, fs.countIn(kFrom));
}

TEST(MoveImagesByFavorite, DoesNotDoubleCountAStuckFileAcrossPasses) {
  // The stuck file is re-selected by each pass, so an accumulating failure count
  // would inflate. The report must show the true number of stuck files.
  FakeFs fs = makeFolder(10, 0);
  fs.unmovable.insert("fav0_F.bmp");

  const auto report = moveImagesByFavorite(fs, kFrom, kTo, true, 4, 0, nullptr);

  EXPECT_EQ(9u, report.moved);
  EXPECT_EQ(1u, report.failed);
}

TEST(MoveImagesByFavorite, TreatsZeroBatchSizeAsOne) {
  FakeFs fs = makeFolder(3, 0);
  const auto report = moveImagesByFavorite(fs, kFrom, kTo, true, 0, 0, nullptr);
  EXPECT_EQ(3u, report.moved);
}

namespace {
size_t yieldCalls = 0;
void countYield() { yieldCalls++; }
}  // namespace

TEST(MoveImagesByFavorite, FeedsTheWatchdogOnTheRequestedInterval) {
  yieldCalls = 0;
  FakeFs fs = makeFolder(20, 0);
  moveImagesByFavorite(fs, kFrom, kTo, true, 8, 5, &countYield);
  // 20 moves, one yield every 5.
  EXPECT_EQ(4u, yieldCalls);
}

TEST(MoveImagesByFavorite, IgnoresNonWallpaperNeighbours) {
  FakeFs fs;
  fs.paths.insert("/sleep/a_F.bmp");
  fs.paths.insert("/sleep/notes_F.txt");

  const auto report = moveImagesByFavorite(fs, kFrom, kTo, true, 8, 0, nullptr);

  EXPECT_EQ(1u, report.moved);
  EXPECT_EQ(1u, fs.paths.count("/sleep/notes_F.txt"));
}
