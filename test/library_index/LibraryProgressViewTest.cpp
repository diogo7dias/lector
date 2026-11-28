#include <gtest/gtest.h>
#include <unistd.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Epub.h"
#include "HalStorage.h"
#include "LibraryBuilder.h"
#include "LibraryFormat.h"
#include "LibraryIndexFile.h"

namespace fs = std::filesystem;
using namespace library;

namespace {

class LibraryProgressViewTest : public ::testing::Test {
 protected:
  void SetUp() override {
    rootDir = fs::temp_directory_path() / ("lector-libview-" + std::to_string(::getpid()) + "-" +
                                           ::testing::UnitTest::GetInstance()->current_test_info()->name());
    fs::remove_all(rootDir);
    fs::create_directories(rootDir);
    fake::root = rootDir.string();
    fake::duplicates.clear();
    fake::failRenameCount = -1;
    fake::failWriteBytes = -1;
    fake_epub::metadata.clear();
    fake_epub::parses = 0;
  }
  void TearDown() override { fs::remove_all(rootDir); }

  void add(const std::string& path, const std::string& title = "", const std::string& author = "") {
    fs::create_directories(fs::path(fake::real(path)).parent_path());
    std::ofstream(fake::real(path), std::ios::binary) << "book";
    if (!title.empty() || !author.empty()) fake_epub::metadata[path] = {title, author};
  }

  fs::path rootDir;
};

TEST_F(LibraryProgressViewTest, StoresProgressInClxV2AndUpdatesInPlace) {
  add("/a.epub", "Alpha", "Author A");
  add("/b.epub", "Beta", "Author B");
  add("/c.epub", "Gamma", "Author C");

  static std::map<std::string, std::pair<uint8_t, uint32_t>> progressMap;
  progressMap.clear();
  progressMap["/a.epub"] = {42, 10};
  progressMap["/b.epub"] = {100, 5};
  auto reader = [](const char* path, uint8_t& pct, uint32_t& order) -> bool {
    auto it = progressMap.find(path);
    if (it == progressMap.end()) return false;
    pct = it->second.first;
    order = it->second.second;
    return true;
  };

  BuildStats stats;
  ASSERT_TRUE(buildLibraryIndex("/", libraryIndexPath(), stats, nullptr, nullptr, true, reader));
  {
    LibraryIndexFile index;
    ASSERT_TRUE(index.open(libraryIndexPath()));
    EXPECT_EQ(index.header().formatVersion, 2);
    ClixRecord r[3];
    ASSERT_EQ(index.readRecords(0, 3, r), 3u);
    EXPECT_EQ(r[0].percent, 42);
    EXPECT_EQ(r[0].readOrder, 10u);
    EXPECT_EQ(r[1].percent, 100);
    EXPECT_EQ(r[1].readOrder, 5u);
    EXPECT_EQ(r[2].percent, CLIX_PERCENT_UNREAD);
    EXPECT_EQ(r[2].readOrder, 0u);
  }

  // Update /c.epub in place without rebuilding the index.
  ASSERT_TRUE(updateRecordProgress(libraryIndexPath(), "/c.epub", 75, 20));
  EXPECT_FALSE(updateRecordProgress(libraryIndexPath(), "/missing.epub", 50, 1));
  {
    LibraryIndexFile index;
    ASSERT_TRUE(index.open(libraryIndexPath()));
    ClixRecord r;
    ASSERT_TRUE(index.readRecord(2, r));
    EXPECT_EQ(r.percent, 75);
    EXPECT_EQ(r.readOrder, 20u);
  }

  // Mark /b.epub unread in place.
  ASSERT_TRUE(updateRecordProgress(libraryIndexPath(), "/b.epub", CLIX_PERCENT_UNREAD, 0));
  {
    LibraryIndexFile index;
    ASSERT_TRUE(index.open(libraryIndexPath()));
    ClixRecord r;
    ASSERT_TRUE(index.readRecord(1, r));
    EXPECT_TRUE(isRecordUnread(r));
    EXPECT_EQ(r.readOrder, 0u);
  }
}

TEST_F(LibraryProgressViewTest, FiltersAndSortsRespectProgressAuthorAndDates) {
  add("/a.epub", "Alpha", "Zebra Author");
  add("/b.epub", "Beta", "Aardvark Author");
  add("/c.epub", "Gamma", "");
  add("/d.epub", "Delta", "Middle Author");

  static std::map<std::string, std::pair<uint8_t, uint32_t>> progressMap;
  progressMap.clear();
  progressMap["/a.epub"] = {50, 10};
  progressMap["/b.epub"] = {100, 30};
  progressMap["/d.epub"] = {0, 20};
  auto reader = [](const char* path, uint8_t& pct, uint32_t& order) -> bool {
    auto it = progressMap.find(path);
    if (it == progressMap.end()) return false;
    pct = it->second.first;
    order = it->second.second;
    return true;
  };

  BuildStats stats;
  ASSERT_TRUE(buildLibraryIndex("/", libraryIndexPath(), stats, nullptr, nullptr, true, reader));
  {
    std::fstream f(fake::real(libraryIndexPath()), std::ios::in | std::ios::out | std::ios::binary);
    const uint32_t mods[4] = {100, 400, 200, 300};
    for (int i = 0; i < 4; ++i) {
      f.seekp(512 + i * sizeof(ClixRecord) + offsetof(ClixRecord, modified));
      f.write(reinterpret_cast<const char*>(&mods[i]), sizeof(uint32_t));
    }
  }

  LibraryIndexFile index;
  ASSERT_TRUE(index.open(libraryIndexPath()));
  auto titlesFor = [&](LibrarySort sort, LibraryFilter filter) {
    std::vector<uint16_t> ord;
    EXPECT_TRUE(buildLibraryView(index, sort, filter, ord));
    std::vector<std::string> titles;
    if (sort == LibrarySort::Title && filter == LibraryFilter::All) {
      EXPECT_TRUE(ord.empty());
      return titles;
    }
    for (uint16_t o : ord) {
      ClixRecord rec;
      std::string t;
      EXPECT_TRUE(index.readRecord(o, rec));
      EXPECT_TRUE(index.readTitle(rec, t));
      titles.push_back(t);
    }
    return titles;
  };

  EXPECT_TRUE(titlesFor(LibrarySort::Title, LibraryFilter::All).empty());
  EXPECT_EQ(titlesFor(LibrarySort::Title, LibraryFilter::Reading), (std::vector<std::string>{"Alpha", "Delta"}));
  EXPECT_EQ(titlesFor(LibrarySort::Title, LibraryFilter::Read), (std::vector<std::string>{"Beta"}));
  EXPECT_EQ(titlesFor(LibrarySort::Title, LibraryFilter::Unread), (std::vector<std::string>{"Gamma"}));
  EXPECT_EQ(titlesFor(LibrarySort::Author, LibraryFilter::All),
            (std::vector<std::string>{"Beta", "Delta", "Alpha", "Gamma"}));
  EXPECT_EQ(titlesFor(LibrarySort::LastRead, LibraryFilter::All),
            (std::vector<std::string>{"Beta", "Delta", "Alpha", "Gamma"}));
  EXPECT_EQ(titlesFor(LibrarySort::RecentlyAdded, LibraryFilter::All),
            (std::vector<std::string>{"Beta", "Gamma", "Delta", "Alpha"}));
  EXPECT_EQ(titlesFor(LibrarySort::LastRead, LibraryFilter::Reading), (std::vector<std::string>{"Delta", "Alpha"}));
}

}  // namespace
