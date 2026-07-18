#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "sleep/SleepFavoriteMove.h"

using crosspoint::sleep::countSleepImagesByFavorite;
using crosspoint::sleep::ISleepFs;
using crosspoint::sleep::moveSleepImagesByFavorite;
using crosspoint::sleep::moveSleepPauseImagesByFavorite;

namespace {

// In-memory /sleep + /sleep pause. rename() moves a basename between the two
// name sets in EITHER direction, mirroring the SD behavior the mover relies on
// (a moved file no longer appears in the source folder's next walk).
class FakeSleepFs : public ISleepFs {
 public:
  std::vector<std::string> sleepNames;  // basenames under /sleep
  std::vector<std::string> pauseNames;  // basenames under /sleep pause
  bool pauseDirMade = false;
  bool sleepDirMade = false;
  int renameCalls = 0;

  size_t countSleepBmps(size_t scanCap) override { return std::min(sleepNames.size(), scanCap); }
  std::vector<std::string> listSleepBmps(size_t maxEntries) override {
    std::vector<std::string> out(sleepNames.begin(), sleepNames.begin() + std::min(maxEntries, sleepNames.size()));
    return out;
  }
  std::string nextSleepBmpAfter(const std::string&) override { return {}; }
  std::string nthSleepBmp(size_t) override { return {}; }
  void walkSleepBmps(const std::function<void(const char*, size_t, uint32_t)>& cb) override {
    for (const auto& n : sleepNames) cb(n.c_str(), n.size(), 0);
  }
  void walkPauseBmps(const std::function<void(const char*, size_t, uint32_t)>& cb) override {
    for (const auto& n : pauseNames) cb(n.c_str(), n.size(), 0);
  }
  bool exists(const std::string& path) override {
    return has(sleepNames, "/sleep/", path) || has(pauseNames, "/sleep pause/", path);
  }
  bool mkdir(const std::string& path) override {
    if (path == "/sleep pause") pauseDirMade = true;
    if (path == "/sleep") sleepDirMade = true;
    return true;
  }
  bool rename(const std::string& from, const std::string& to) override {
    ++renameCalls;
    // Route by source prefix so the fake handles both move directions.
    std::vector<std::string>* src = nullptr;
    std::vector<std::string>* dst = nullptr;
    std::string base;
    if (strip(from, "/sleep pause/", base)) {
      src = &pauseNames;
      dst = &sleepNames;
    } else if (strip(from, "/sleep/", base)) {
      src = &sleepNames;
      dst = &pauseNames;
    } else {
      return false;
    }
    auto it = std::find(src->begin(), src->end(), base);
    if (it == src->end()) return false;
    // Destination collision check: `to` must not already exist.
    const std::string destPrefix = (dst == &sleepNames) ? "/sleep/" : "/sleep pause/";
    if (has(*dst, destPrefix, to)) return false;  // no overwrite, like SdFat
    src->erase(it);
    dst->push_back(base);
    return true;
  }

 private:
  static bool strip(const std::string& path, const std::string& prefix, std::string& baseOut) {
    if (path.rfind(prefix, 0) != 0) return false;
    baseOut = path.substr(prefix.size());
    return true;
  }
  static bool has(const std::vector<std::string>& names, const std::string& prefix, const std::string& fullPath) {
    std::string base;
    if (!strip(fullPath, prefix, base)) return false;
    return std::find(names.begin(), names.end(), base) != names.end();
  }
};

// Favorite iff the basename carries the _F suffix before its extension.
bool isFavBySuffix(const std::string& name) {
  const auto dot = name.find_last_of('.');
  const std::string stem = (dot == std::string::npos) ? name : name.substr(0, dot);
  return stem.size() >= 2 && stem.compare(stem.size() - 2, 2, "_F") == 0;
}

}  // namespace

TEST(SleepFavoriteMove, MovesOnlyNonFavoritesLeavingFavorites) {
  FakeSleepFs fs;
  fs.sleepNames = {"a.bmp", "b_F.bmp", "c.pxc", "d_F.pxc", "e.bmp"};

  const size_t moved = moveSleepImagesByFavorite(fs, isFavBySuffix, nullptr, /*moveFavorites=*/false,
                                                 /*batchSize=*/2, /*yieldEvery=*/0, nullptr);

  EXPECT_EQ(moved, 3u);  // a, c, e
  EXPECT_TRUE(fs.pauseDirMade);
  // Only favorites remain in /sleep.
  std::vector<std::string> remaining = fs.sleepNames;
  std::sort(remaining.begin(), remaining.end());
  EXPECT_EQ(remaining, (std::vector<std::string>{"b_F.bmp", "d_F.pxc"}));
  std::vector<std::string> paused = fs.pauseNames;
  std::sort(paused.begin(), paused.end());
  EXPECT_EQ(paused, (std::vector<std::string>{"a.bmp", "c.pxc", "e.bmp"}));
}

TEST(SleepFavoriteMove, MovesOnlyFavorites) {
  FakeSleepFs fs;
  fs.sleepNames = {"a.bmp", "b_F.bmp", "c.pxc", "d_F.pxc"};

  const size_t moved = moveSleepImagesByFavorite(fs, isFavBySuffix, nullptr, /*moveFavorites=*/true,
                                                 /*batchSize=*/8, /*yieldEvery=*/0, nullptr);

  EXPECT_EQ(moved, 2u);  // b_F, d_F
  std::vector<std::string> remaining = fs.sleepNames;
  std::sort(remaining.begin(), remaining.end());
  EXPECT_EQ(remaining, (std::vector<std::string>{"a.bmp", "c.pxc"}));
}

TEST(SleepFavoriteMove, BatchingMovesEverythingAndFiresCallbackPerMove) {
  FakeSleepFs fs;
  for (int i = 0; i < 10; ++i) fs.sleepNames.push_back("img" + std::to_string(i) + ".bmp");  // all non-fav

  int callbacks = 0;
  const size_t moved = moveSleepImagesByFavorite(
      fs, isFavBySuffix, [&](const std::string&, const std::string&) { ++callbacks; }, /*moveFavorites=*/false,
      /*batchSize=*/3, /*yieldEvery=*/0, nullptr);

  EXPECT_EQ(moved, 10u);
  EXPECT_EQ(callbacks, 10);
  EXPECT_TRUE(fs.sleepNames.empty());
}

TEST(SleepFavoriteMove, StopsWhenNoMatches) {
  FakeSleepFs fs;
  fs.sleepNames = {"a_F.bmp", "b_F.bmp"};  // all favorites; moving non-favorites is a no-op

  const size_t moved = moveSleepImagesByFavorite(fs, isFavBySuffix, nullptr, /*moveFavorites=*/false,
                                                 /*batchSize=*/4, /*yieldEvery=*/0, nullptr);

  EXPECT_EQ(moved, 0u);
  EXPECT_FALSE(fs.pauseDirMade);  // never created the folder when nothing moves
  EXPECT_EQ(fs.sleepNames.size(), 2u);
}

TEST(SleepFavoriteMove, NameCollisionDoesNotLoopForever) {
  FakeSleepFs fs;
  fs.sleepNames = {"dup.bmp"};
  fs.pauseNames = {"dup.bmp"};  // destination already taken -> rename fails

  const size_t moved = moveSleepImagesByFavorite(fs, isFavBySuffix, nullptr, /*moveFavorites=*/false,
                                                 /*batchSize=*/4, /*yieldEvery=*/0, nullptr);

  EXPECT_EQ(moved, 0u);
  EXPECT_EQ(fs.sleepNames.size(), 1u);  // still there, but the loop terminated
}

TEST(SleepFavoriteMove, YieldFiresOnCadence) {
  FakeSleepFs fs;
  for (int i = 0; i < 7; ++i) fs.sleepNames.push_back("n" + std::to_string(i) + ".bmp");

  int yields = 0;
  moveSleepImagesByFavorite(fs, isFavBySuffix, nullptr, /*moveFavorites=*/false, /*batchSize=*/16,
                            /*yieldEvery=*/2, [&] { ++yields; });

  EXPECT_EQ(yields, 3);  // fired at 2, 4, 6 of 7 moves
}

TEST(SleepFavoriteMove, ReverseMovesOnlyFavoritesBackToSleep) {
  FakeSleepFs fs;
  fs.pauseNames = {"a.bmp", "b_F.bmp", "c.pxc", "d_F.pxc", "e.bmp"};

  const size_t moved = moveSleepPauseImagesByFavorite(fs, isFavBySuffix, nullptr, /*moveFavorites=*/true,
                                                      /*batchSize=*/2, /*yieldEvery=*/0, nullptr);

  EXPECT_EQ(moved, 2u);  // b_F, d_F
  EXPECT_TRUE(fs.sleepDirMade);
  // Only non-favorites remain in /sleep pause.
  std::vector<std::string> remaining = fs.pauseNames;
  std::sort(remaining.begin(), remaining.end());
  EXPECT_EQ(remaining, (std::vector<std::string>{"a.bmp", "c.pxc", "e.bmp"}));
  // Favorites landed back in /sleep.
  std::vector<std::string> back = fs.sleepNames;
  std::sort(back.begin(), back.end());
  EXPECT_EQ(back, (std::vector<std::string>{"b_F.bmp", "d_F.pxc"}));
}

TEST(SleepFavoriteMove, ReverseBatchingMovesEveryFavorite) {
  FakeSleepFs fs;
  for (int i = 0; i < 10; ++i) fs.pauseNames.push_back("img" + std::to_string(i) + "_F.bmp");  // all favorites

  int callbacks = 0;
  const size_t moved = moveSleepPauseImagesByFavorite(
      fs, isFavBySuffix, [&](const std::string&, const std::string&) { ++callbacks; }, /*moveFavorites=*/true,
      /*batchSize=*/3, /*yieldEvery=*/0, nullptr);

  EXPECT_EQ(moved, 10u);
  EXPECT_EQ(callbacks, 10);
  EXPECT_TRUE(fs.pauseNames.empty());
  EXPECT_EQ(fs.sleepNames.size(), 10u);
}

TEST(SleepFavoriteMove, ReverseCollisionDoesNotLoopForever) {
  FakeSleepFs fs;
  fs.pauseNames = {"dup_F.bmp"};
  fs.sleepNames = {"dup_F.bmp"};  // destination already taken -> rename fails

  const size_t moved = moveSleepPauseImagesByFavorite(fs, isFavBySuffix, nullptr, /*moveFavorites=*/true,
                                                      /*batchSize=*/4, /*yieldEvery=*/0, nullptr);

  EXPECT_EQ(moved, 0u);
  EXPECT_EQ(fs.pauseNames.size(), 1u);  // still there, loop terminated
}

TEST(SleepFavoriteMove, CountByFavoriteRespectsCap) {
  FakeSleepFs fs;
  fs.sleepNames = {"a.bmp", "b_F.bmp", "c.bmp", "d.bmp", "e_F.bmp"};

  EXPECT_EQ(countSleepImagesByFavorite(fs, isFavBySuffix, /*favorites=*/false, /*scanCap=*/100), 3u);
  EXPECT_EQ(countSleepImagesByFavorite(fs, isFavBySuffix, /*favorites=*/true, /*scanCap=*/100), 2u);
  EXPECT_EQ(countSleepImagesByFavorite(fs, isFavBySuffix, /*favorites=*/false, /*scanCap=*/2), 2u);
}
