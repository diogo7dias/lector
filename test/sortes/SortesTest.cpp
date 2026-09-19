#include <gtest/gtest.h>
#include <unistd.h>

#include <fstream>
#include <iterator>
#include <set>

#include "activities/reader/EpubReaderUtils.h"
#include "util/BookFilingNames.h"
#include "util/BookProgressFile.h"
#include "util/Sortes.h"

std::string bookCacheDirForPath(const std::string& path) { return bookfiling::cacheDirFor(path); }

namespace {
uint32_t seed;
uint32_t randomWord() {
  seed = seed * 1664525u + 1013904223u;
  return seed;
}
std::string bytes(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}
void put(const std::string& path, const std::string& value) {
  const auto dest = Storage.resolve(path);
  std::filesystem::create_directories(dest.parent_path());
  std::ofstream(dest, std::ios::binary) << value;
}
class SortesTest : public testing::Test {
 protected:
  void SetUp() override {
    char dir[] = "/tmp/lector-sortes-test-XXXXXX";
    ASSERT_NE(mkdtemp(dir), nullptr);
    Storage.root = dir;
    Storage.mutations = 0;
    seed = 42;
  }
  void TearDown() override { std::filesystem::remove_all(Storage.root); }
  void book(const std::string& path, int percent) {
    put(path, "book");
    if (percent >= 0) put(bookCacheDirForPath(path) + "/percent.bin", std::string(1, char(percent)));
  }
};
TEST_F(SortesTest, EmptyOrUnfinishedLibraryNeverProducesABook) {
  std::string selected = "stale";
  EXPECT_EQ(sortes::findBook(selected, randomWord), sortes::ScanResult::Empty);
  EXPECT_TRUE(selected.empty());
  book("/read/unfinished.epub", 99);  // folder membership cannot override progress
  book("/unknown.epub", -1);
  book("/broken.epub", 101);
  EXPECT_EQ(sortes::findBook(selected, randomWord), sortes::ScanResult::Empty);
  EXPECT_TRUE(selected.empty());
}
TEST_F(SortesTest, ScansEveryFolderAndNeverSelectsUnfinishedBooks) {
  const std::set<std::string> eligible = {"/a.epub", "/books/nested/b.epub", "/read/c.epub", "/z.epub"};
  for (const auto& path : eligible) book(path, 100);
  book("/read/unfinished.epub", 20);
  book("/.crosspoint/ignored.epub", 100);
  std::set<std::string> seen;
  for (int i = 0; i < 100; ++i) {
    seed = static_cast<uint32_t>(i);
    std::string selected;
    ASSERT_EQ(sortes::findBook(selected, randomWord), sortes::ScanResult::Found);
    EXPECT_TRUE(eligible.count(selected)) << selected;
    seen.insert(selected);
  }
  EXPECT_EQ(seen, eligible);
  EXPECT_EQ(Storage.mutations, 0);
}
TEST_F(SortesTest, SelectionIsNotLimitedToRecentBooksOrFirstN) {
  for (int i = 0; i < 300; ++i) book("/books/book" + std::to_string(i) + ".epub", 100);
  std::set<std::string> seen;
  for (int i = 0; i < 500; ++i) {
    seed = i;
    std::string selected;
    ASSERT_EQ(sortes::findBook(selected, randomWord), sortes::ScanResult::Found);
    seen.insert(selected);
  }
  EXPECT_GT(seen.size(), 200u);
}
TEST_F(SortesTest, ScanOverflowRefusesPartialSelection) {
  book("/a.epub", 100);
  std::string deep;
  for (int i = 0; i < 33; ++i) deep += "/d";
  book(deep + "/b.epub", 100);
  std::string selected;
  EXPECT_EQ(sortes::findBook(selected, randomWord), sortes::ScanResult::Failed);
  EXPECT_TRUE(selected.empty());
}
TEST_F(SortesTest, SavedProgressRemainsByteIdenticalAcrossSortesSaves) {
  const Epub epub("/.crosspoint/epub_test");
  for (const size_t size : {4u, 6u, 10u}) {
    const std::string saved("\x07\x00\x31\x00\x48\x00\xFF\x42\x80\x01", size);
    put(epub.getCachePath() + "/progress.bin", saved);
    put(epub.getCachePath() + "/percent.bin", std::string("\x64\x07\x00\x00\x00", 5));
    const auto beforeMarker = bytes(Storage.resolve(epub.getCachePath() + "/percent.bin"));
    for (int page = 0; page < 50; ++page) {
      ASSERT_TRUE(EpubReaderUtils::saveProgress(epub, 2, page, 80, page * 100, true));
    }
    // Explicit exit/footnote saves, including at end-of-book.
    ASSERT_TRUE(EpubReaderUtils::saveProgress(epub, 3, 0, 0, std::nullopt, true));
    ASSERT_TRUE(EpubReaderUtils::saveProgress(epub, 99, 0, 0, std::nullopt, true));
    EXPECT_EQ(bytes(Storage.resolve(epub.getCachePath() + "/progress.bin")), saved);
    EXPECT_EQ(bytes(Storage.resolve(epub.getCachePath() + "/percent.bin")), beforeMarker);
    EXPECT_EQ(Storage.mutations, 0);
    EXPECT_FALSE(std::filesystem::exists(Storage.resolve(epub.getCachePath() + "/progress.bin.tmp")));
  }
  ASSERT_TRUE(EpubReaderUtils::saveProgress(epub, 1, 2, 3));
  EXPECT_EQ(bytes(Storage.resolve(epub.getCachePath() + "/progress.bin")), std::string("\1\0\2\0\3\0", 6));
  EXPECT_GT(Storage.mutations, 0);  // the same writer still persists ordinary reading
}
TEST(SortesRandom, RejectsBiasedTailAndReturnsRealPageIndices) {
  static int calls;
  calls = 0;
  EXPECT_EQ(sortes::below(10, []() -> uint32_t { return calls++ == 0 ? 0 : 19; }), 9u);
  EXPECT_EQ(calls, 2);
  for (uint32_t count : {1u, 2u, 13u, 65535u}) {
    for (int i = 0; i < 1000; ++i) EXPECT_LT(sortes::below(count, randomWord), count);
  }
}
// The hardware activity cannot run on the host. Check that it actually passes the
// tested mode into the production writer and doesn't reach the exit/history sinks.
TEST(SortesWiring, EntryExitSleepAndMenuWritesAreGated) {
  const auto source = bytes(std::string(SORTES_REPO) + "/src/activities/reader/EpubReaderActivity.cpp");
  EXPECT_NE(source.find("pageCount, offset, sortesMode)"), std::string::npos);
  EXPECT_NE(source.find("statsTrackingActive = !sortesMode &&"), std::string::npos);
  EXPECT_NE(source.find("if (!sortesMode && SETTINGS.removeReadBooksFromRecents)"), std::string::npos);
  const auto exit = source.substr(source.find("void EpubReaderActivity::onExit()"));
  const auto gate = exit.find("if (sortesMode)");
  ASSERT_NE(gate, std::string::npos);
  EXPECT_LT(gate, exit.find("book_progress::write"));
  EXPECT_LT(gate, exit.find("RECENT_BOOKS.setProgress"));
  EXPECT_LT(gate, exit.find("bookfiling::moveBookToFolder"));
  EXPECT_NE(exit.substr(gate, exit.find("APP_STATE.readerActivityLoadCount") - gate).find("return;"),
            std::string::npos);
  EXPECT_NE(exit.find("APP_STATE.lastSleepFromReader = false;"), std::string::npos);
  EXPECT_NE(source.find("if (blockSortesAction()) return true;"), std::string::npos);
  EXPECT_NE(source.find("MenuAction::DELETE_CACHE ||"), std::string::npos);
}
}  // namespace
