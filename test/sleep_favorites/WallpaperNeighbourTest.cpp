// Host tests for the streaming next/previous wallpaper lookup.
//
// The viewer uses this to flick through a folder without ever holding the folder
// listing in memory, so the ordering has to be right from a single pass with one
// candidate retained.
#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "sleep/WallpaperNeighbour.h"

using crosspoint::sleep::ISleepImageFs;
using crosspoint::sleep::NameSink;
using crosspoint::sleep::neighbourWallpaper;

namespace {

constexpr const char* kDir = "/sleep";

class ListFs final : public ISleepImageFs {
 public:
  std::vector<std::string> names;  // deliberately unsorted: the SD walk is not sorted either

  void walk(const char* dir, const NameSink& sink) override {
    if (std::string(dir) != kDir) return;
    for (const auto& name : names) sink(name.c_str(), name.size());
  }
  bool mkdir(const char*) override { return true; }
  bool rename(const char*, const char*) override { return true; }
};

ListFs threeUnsorted() {
  ListFs fs;
  fs.names = {"c.bmp", "a.bmp", "b.bmp"};
  return fs;
}

}  // namespace

TEST(NeighbourWallpaper, FindsTheNextNameRegardlessOfWalkOrder) {
  ListFs fs = threeUnsorted();
  EXPECT_EQ("b.bmp", neighbourWallpaper(fs, kDir, "a.bmp", true));
  EXPECT_EQ("c.bmp", neighbourWallpaper(fs, kDir, "b.bmp", true));
}

TEST(NeighbourWallpaper, FindsThePreviousName) {
  ListFs fs = threeUnsorted();
  EXPECT_EQ("b.bmp", neighbourWallpaper(fs, kDir, "c.bmp", false));
  EXPECT_EQ("a.bmp", neighbourWallpaper(fs, kDir, "b.bmp", false));
}

TEST(NeighbourWallpaper, DoesNotWrapAtTheEnds) {
  // The viewer shows "<" and ">" hints from these results, so the ends must be
  // reported honestly rather than looping the user back around.
  ListFs fs = threeUnsorted();
  EXPECT_EQ("", neighbourWallpaper(fs, kDir, "c.bmp", true));
  EXPECT_EQ("", neighbourWallpaper(fs, kDir, "a.bmp", false));
}

TEST(NeighbourWallpaper, HandlesANameThatIsNotInTheFolder) {
  // The current file can vanish (deleted from the browser, or renamed by a
  // favourite toggle in another screen). Neighbour lookup still has to work.
  ListFs fs = threeUnsorted();
  EXPECT_EQ("c.bmp", neighbourWallpaper(fs, kDir, "bb.bmp", true));
  EXPECT_EQ("b.bmp", neighbourWallpaper(fs, kDir, "bb.bmp", false));
}

TEST(NeighbourWallpaper, EmptyFolderHasNoNeighbour) {
  ListFs fs;
  EXPECT_EQ("", neighbourWallpaper(fs, kDir, "a.bmp", true));
  EXPECT_EQ("", neighbourWallpaper(fs, kDir, "a.bmp", false));
}

TEST(NeighbourWallpaper, SingleFileHasNoNeighbourEitherWay) {
  ListFs fs;
  fs.names = {"only.pxc"};
  EXPECT_EQ("", neighbourWallpaper(fs, kDir, "only.pxc", true));
  EXPECT_EQ("", neighbourWallpaper(fs, kDir, "only.pxc", false));
}

TEST(NeighbourWallpaper, UnknownFolderYieldsNothing) {
  ListFs fs = threeUnsorted();
  EXPECT_EQ("", neighbourWallpaper(fs, "/sleep pause", "a.bmp", true));
}
