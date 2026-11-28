#include <LibraryBuilder.h>
#include <LibraryFormat.h>
#include <LibraryGroups.h>
#include <LibraryIndexFile.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "stubs/Epub.h"
#include "stubs/HalStorage.h"

namespace {

namespace fs = std::filesystem;
using namespace library;

class LibrarySeriesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    rootDir = fs::temp_directory_path() / ("lector-libseries-" + std::to_string(::getpid()) + "-" +
                                           ::testing::UnitTest::GetInstance()->current_test_info()->name());
    fs::remove_all(rootDir);
    fs::create_directories(rootDir);
    fake::root = rootDir.string();
    fake::duplicates.clear();
    fake::failRenameCount = -1;
    fake::failWriteBytes = -1;
    fake_epub::metadata.clear();
    fake_epub::series.clear();
    fake_epub::parses = 0;
  }

  void TearDown() override { fs::remove_all(rootDir); }

  void addBook(const char* path, const char* title, const char* author, const char* series = "",
               const uint16_t seriesIdxX10 = 0) {
    fs::create_directories(fs::path(fake::real(path)).parent_path());
    std::ofstream(fake::real(path), std::ios::binary) << "book";
    fake_epub::metadata[path] = {title, author};
    if (series && series[0]) fake_epub::series[path] = {series, seriesIdxX10};
  }

  fs::path rootDir;
};

TEST_F(LibrarySeriesTest, StoresSeriesInClxV2AndReusesOnRebuild) {
  addBook("/a.epub", "Dune Messiah", "Frank Herbert", "Dune", 20);
  addBook("/b.epub", "Dune", "Frank Herbert", "Dune", 10);
  addBook("/c.epub", "The Left Hand of Darkness", "Ursula K. Le Guin");

  BuildStats stats;
  ASSERT_TRUE(buildLibraryIndex("/", libraryIndexPath(), stats));
  EXPECT_EQ(stats.books, 3);
  EXPECT_EQ(stats.parsed, 3u);

  LibraryIndexFile index;
  ASSERT_TRUE(index.open(libraryIndexPath()));
  EXPECT_TRUE(index.hasSeriesMetadata());

  ClixRecord r{};
  std::string title, series;
  ASSERT_TRUE(index.readRecord(0, r));  // "Dune"
  ASSERT_TRUE(index.readTitle(r, title));
  ASSERT_TRUE(index.readSeries(r, series));
  EXPECT_EQ(title, "Dune");
  EXPECT_EQ(series, "Dune");
  EXPECT_EQ(r.seriesIndexX10, 10);

  // Second build with unchanged files reuses series without re-parsing EPUBs.
  fake_epub::parses = 0;
  ASSERT_TRUE(buildLibraryIndex("/", libraryIndexPath(), stats));
  EXPECT_EQ(stats.reused, 3u);
  EXPECT_EQ(fake_epub::parses, 0u);
}

TEST_F(LibrarySeriesTest, BuildsAuthorAndSeriesGroupsAndSortsSeriesByIndex) {
  addBook("/d3.epub", "Children of Dune", "Frank Herbert", "Dune", 30);
  addBook("/d1.epub", "Dune", "Frank Herbert", "Dune", 10);
  addBook("/d2.epub", "Dune Messiah", "Frank Herbert", "Dune", 20);
  addBook("/d15.epub", "Dune Novella", "Frank Herbert", "Dune", 15);
  addBook("/f1.epub", "Foundation", "Isaac Asimov", "Foundation", 10);

  BuildStats stats;
  ASSERT_TRUE(buildLibraryIndex("/", libraryIndexPath(), stats));

  LibraryIndexFile index;
  ASSERT_TRUE(index.open(libraryIndexPath()));

  std::vector<GroupEntry> authors;
  ASSERT_TRUE(buildGroups(index, GroupKind::Authors, authors));
  ASSERT_EQ(authors.size(), 2u);
  EXPECT_EQ(authors[0].name, "Frank Herbert");
  EXPECT_EQ(authors[0].count, 4u);
  EXPECT_EQ(authors[1].name, "Isaac Asimov");
  EXPECT_EQ(authors[1].count, 1u);

  std::vector<GroupEntry> seriesList;
  ASSERT_TRUE(buildGroups(index, GroupKind::Series, seriesList));
  ASSERT_EQ(seriesList.size(), 2u);
  EXPECT_EQ(seriesList[0].name, "Dune");
  EXPECT_EQ(seriesList[0].count, 4u);

  std::vector<uint16_t> duneBooks;
  ASSERT_TRUE(collectBooksInGroup(index, GroupKind::Series, "Dune", duneBooks));
  ASSERT_EQ(duneBooks.size(), 4u);

  std::vector<std::string> titles;
  for (const uint16_t ord : duneBooks) {
    ClixRecord rec{};
    std::string t;
    ASSERT_TRUE(index.readRecord(ord, rec));
    ASSERT_TRUE(index.readTitle(rec, t));
    titles.push_back(t);
  }
  EXPECT_EQ(titles, (std::vector<std::string>{"Dune", "Dune Novella", "Dune Messiah", "Children of Dune"}));
}

TEST_F(LibrarySeriesTest, FindNextInSeriesPrefersSeriesIndexPlusOne) {
  addBook("/d1.epub", "Dune", "Frank Herbert", "Dune", 10);
  addBook("/d15.epub", "Dune Novella", "Frank Herbert", "Dune", 15);
  addBook("/d2.epub", "Dune Messiah", "Frank Herbert", "Dune", 20);
  addBook("/d3.epub", "Children of Dune", "Frank Herbert", "Dune", 30);

  BuildStats stats;
  ASSERT_TRUE(buildLibraryIndex("/", libraryIndexPath(), stats));

  LibraryIndexFile index;
  ASSERT_TRUE(index.open(libraryIndexPath()));

  std::string nextPath;
  // From #1 (10), exact #2 (20 = 10 + 10) wins over #1.5 (15).
  ASSERT_TRUE(findNextInSeries(index, "/d1.epub", nextPath));
  EXPECT_EQ(nextPath, "/d2.epub");

  // From #2 (20), finds #3 (30).
  ASSERT_TRUE(findNextInSeries(index, "/d2.epub", nextPath));
  EXPECT_EQ(nextPath, "/d3.epub");

  // From last book #3 (30), returns false.
  EXPECT_FALSE(findNextInSeries(index, "/d3.epub", nextPath));

  char badge[16];
  formatSeriesBadge(10, badge, sizeof(badge));
  EXPECT_STREQ(badge, "#1");
  formatSeriesBadge(15, badge, sizeof(badge));
  EXPECT_STREQ(badge, "#1.5");
  formatSeriesBadge(0, badge, sizeof(badge));
  EXPECT_STREQ(badge, "");
}

}  // namespace
