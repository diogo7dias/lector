#include <FsHelpers.h>
#include <gtest/gtest.h>
#include <malloc.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <new>

#include "Epub.h"
#include "HalStorage.h"
#include "LibraryBuilder.h"
#include "LibraryFormat.h"
#include "LibraryIndexFile.h"

// --- heap meter: every allocation of the process, so a build's peak can be read off ---------------
namespace meter {
std::atomic<size_t> current{0};
std::atomic<size_t> peak{0};
void* take(const size_t size) {
  void* p = std::malloc(size == 0 ? 1 : size);
  if (!p) return nullptr;
  const size_t now = current += malloc_usable_size(p);
  size_t seen = peak.load();
  while (now > seen && !peak.compare_exchange_weak(seen, now)) {
  }
  return p;
}
void give(void* p) {
  if (!p) return;
  current -= malloc_usable_size(p);
  std::free(p);
}
}  // namespace meter

void* operator new(const size_t n) {
  void* p = meter::take(n);
  if (!p) throw std::bad_alloc();
  return p;
}
void* operator new[](const size_t n) { return operator new(n); }
void* operator new(const size_t n, const std::nothrow_t&) noexcept { return meter::take(n); }
void* operator new[](const size_t n, const std::nothrow_t&) noexcept { return meter::take(n); }
void operator delete(void* p) noexcept { meter::give(p); }
void operator delete[](void* p) noexcept { meter::give(p); }
void operator delete(void* p, size_t) noexcept { meter::give(p); }
void operator delete[](void* p, size_t) noexcept { meter::give(p); }

namespace fs = std::filesystem;
using namespace library;

namespace {

struct Row {
  std::string title, author, path;
};

class LibraryIndexTest : public ::testing::Test {
 protected:
  void SetUp() override {
    rootDir = fs::temp_directory_path() / ("lector-library-" + std::to_string(::getpid()) + "-" +
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

  // A book on the card; an EPUB with a title or author is one the stub can read them from.
  void add(const std::string& path, const std::string& title = "", const std::string& author = "",
           const std::string& body = "book") {
    fs::create_directories(fs::path(fake::real(path)).parent_path());
    std::ofstream(fake::real(path), std::ios::binary) << body;
    if (!title.empty() || !author.empty()) fake_epub::metadata[path] = {title, author};
  }

  bool build(BuildStats& stats) { return buildLibraryIndex("/", libraryIndexPath(), stats); }
  bool build() {
    BuildStats stats;
    return build(stats);
  }

  std::vector<Row> rows() {
    std::vector<Row> out;
    LibraryIndexFile index;
    if (!index.open(libraryIndexPath())) return out;
    for (uint16_t i = 0; i < index.bookCount(); i += 24) {
      ClixRecord records[24];
      const size_t got = index.readRecords(i, 24, records);
      for (size_t k = 0; k < got; k++) {
        Row row;
        EXPECT_TRUE(index.readTitle(records[k], row.title));
        EXPECT_TRUE(index.readAuthor(records[k], row.author));
        EXPECT_TRUE(index.readPath(records[k], row.path));
        out.push_back(row);
      }
    }
    return out;
  }

  fs::path rootDir;
};

}  // namespace

// --- format ---------------------------------------------------------------------------------

TEST(LibraryFormat, HeaderValidation) {
  ClixHeader h{};
  std::memcpy(h.magic, CLIX_MAGIC, 4);
  h.formatVersion = CLIX_FORMAT_VERSION;
  h.bookCount = 3;
  layoutSections(h, 100);
  EXPECT_EQ(h.recordStart, 512u);
  EXPECT_EQ(h.nameStart, 1024u);  // 512 + 3 * 32, aligned up
  EXPECT_EQ(h.selfSize, 1124u);
  EXPECT_EQ(validateHeader(h, h.selfSize), ClixValidity::Ok);
  EXPECT_EQ(validateHeader(h, h.selfSize - 1), ClixValidity::SizeMismatch);

  ClixHeader bad = h;
  bad.magic[0] = 'X';
  EXPECT_EQ(validateHeader(bad, h.selfSize), ClixValidity::BadMagic);
  bad = h;
  bad.formatVersion = 1;  // v1 index must be rejected and rebuilt to v2
  EXPECT_EQ(validateHeader(bad, h.selfSize), ClixValidity::UnknownFormatVersion);
  bad = h;
  bad.formatVersion = 3;
  EXPECT_EQ(validateHeader(bad, h.selfSize), ClixValidity::UnknownFormatVersion);
  bad = h;
  bad.bookCount = CLIX_MAX_RECORDS + 1;
  EXPECT_EQ(validateHeader(bad, h.selfSize), ClixValidity::CountOutOfRange);
  bad = h;
  bad.nameStart += 512;  // sections that do not follow from the counts
  EXPECT_EQ(validateHeader(bad, h.selfSize), ClixValidity::SectionsInconsistent);
  bad = h;
  bad.nameLen = 0xFFFFFFF0u;  // would wrap the section sum
  EXPECT_NE(validateHeader(bad, h.selfSize), ClixValidity::Ok);
}

TEST(LibraryFormat, RecordBoundsRejectOutOfRangeText) {
  ClixHeader h{};
  h.nameLen = 100;
  ClixRecord r{};
  r.pathLen = 10;
  r.titleLen = 20;
  r.nameOff = 70;
  EXPECT_TRUE(recordInBounds(h, r));
  r.nameOff = 71;
  EXPECT_FALSE(recordInBounds(h, r));  // runs past the section
  r.nameOff = 0xFFFFFFFFu;
  EXPECT_FALSE(recordInBounds(h, r));  // would wrap a 32-bit sum
  r.nameOff = 0;
  r.pathLen = 0;
  EXPECT_FALSE(recordInBounds(h, r));  // a book with no path cannot be opened
  r.pathLen = 10;
  r.titleLen = CLIX_MAX_TITLE + 1;
  EXPECT_FALSE(recordInBounds(h, r));
}

TEST(LibraryFormat, CleanTextBoundsWhatABookSays) {
  EXPECT_EQ(cleanText("  A \t Title\n\n with\x01 gaps  ", 64), "A Title with gaps");
  EXPECT_EQ(cleanText("\n\n", 64), "");
  EXPECT_EQ(cleanText("abcdef", 3), "abc");
  // A cut inside "é" (2 bytes) or "한" (3 bytes) drops the partial character.
  EXPECT_EQ(cleanText("abé", 3), "ab");
  EXPECT_EQ(cleanText("ab한", 4), "ab");
  EXPECT_EQ(cleanText("ab한", 5), "ab한");
  EXPECT_LE(cleanText(std::string(5000, 'x'), CLIX_MAX_TITLE).size(), CLIX_MAX_TITLE);
  EXPECT_EQ(sortFold("The Hobbit É"), "the hobbit É");
}

// --- build ----------------------------------------------------------------------------------

TEST_F(LibraryIndexTest, SortsByTitleIgnoringCaseAndKeepsAuthors) {
  add("/one.epub", "banana", "B. Author");
  add("/sub/two.epub", "Apple", "A. Author");
  add("/three.epub", "cherry", "Unknown");  // exporters write this
  add("/plain.txt");                        // no metadata: its name is its title
  BuildStats stats;
  ASSERT_TRUE(build(stats));
  EXPECT_EQ(stats.books, 4);
  const auto r = rows();
  ASSERT_EQ(r.size(), 4u);
  EXPECT_EQ(r[0].title, "Apple");
  EXPECT_EQ(r[0].author, "A. Author");
  EXPECT_EQ(r[0].path, "/sub/two.epub");
  EXPECT_EQ(r[1].title, "banana");
  EXPECT_EQ(r[2].title, "cherry");
  EXPECT_EQ(r[2].author, "");
  EXPECT_EQ(r[3].title, "plain");
  EXPECT_EQ(r[3].path, "/plain.txt");
}

TEST_F(LibraryIndexTest, TitlesSharingASortPrefixAreOrderedByTheWholeTitle) {
  const char* series = "Harry Potter and the ";
  add("/a.epub", std::string(series) + "Prisoner of Azkaban");
  add("/b.epub", std::string(series) + "Chamber of Secrets");
  add("/c.epub", std::string(series) + "Philosopher's Stone");
  ASSERT_TRUE(build());
  const auto r = rows();
  ASSERT_EQ(r.size(), 3u);
  EXPECT_EQ(r[0].title, std::string(series) + "Chamber of Secrets");
  EXPECT_EQ(r[1].title, std::string(series) + "Philosopher's Stone");
  EXPECT_EQ(r[2].title, std::string(series) + "Prisoner of Azkaban");
}

TEST_F(LibraryIndexTest, ListsBooksAndNothingElse) {
  add("/a.epub", "A");
  add("/b/c.txt");
  add("/recents/d.xtc");
  add("/e.md");
  add("/.hidden.epub", "hidden");
  add("/._junk.epub", "apple double");
  add("/c_QUOTES.txt");  // the quote grabber's sidecar
  add("/f.epub.part", "half-arrived");
  add("/cover.jpg");
  add("/sleep/x.epub", "wallpaper folder");
  add("/Fonts/y.txt", "fonts folder, any case");
  add("/.crosspoint/z.epub", "cache");
  add("/System Volume Information/s.epub", "os");
  add("/nested/sleep/ok.epub", "sleep is only skipped at the root");
  ASSERT_TRUE(build());
  std::vector<std::string> paths;
  for (const auto& row : rows()) paths.push_back(row.path);
  std::sort(paths.begin(), paths.end());
  EXPECT_EQ(paths,
            (std::vector<std::string>{"/a.epub", "/b/c.txt", "/e.md", "/nested/sleep/ok.epub", "/recents/d.xtc"}));
}

TEST_F(LibraryIndexTest, ReusesWhatItKnowsAndParsesOnlyWhatChanged) {
  for (int i = 0; i < 20; i++) add("/b" + std::to_string(i) + ".epub", "Book " + std::to_string(i), "Someone");
  BuildStats stats;
  ASSERT_TRUE(build(stats));
  EXPECT_EQ(stats.parsed, 20);
  EXPECT_EQ(stats.reused, 0);

  fake_epub::parses = 0;
  ASSERT_TRUE(build(stats));
  EXPECT_EQ(stats.parsed, 0);
  EXPECT_EQ(stats.reused, 20);
  EXPECT_EQ(fake_epub::parses, 0u);
  EXPECT_EQ(rows().size(), 20u);

  // One book replaced with another of a different size (same path): read again, title updated.
  add("/b3.epub", "Book three, revised", "Someone", "a much longer body than before");
  ASSERT_TRUE(build(stats));
  EXPECT_EQ(stats.parsed, 1);
  EXPECT_EQ(stats.reused, 19);
  bool found = false;
  for (const auto& row : rows()) found |= row.title == "Book three, revised";
  EXPECT_TRUE(found);
}

TEST_F(LibraryIndexTest, AMovedBookShowsOnceAtItsNewPath) {
  // What opening a book does: file it into /recents. Finishing it files it into /read.
  add("/a.epub", "Alpha");
  add("/b.epub", "Beta");
  ASSERT_TRUE(build());

  fs::create_directories(fake::real("/recents"));
  fs::rename(fake::real("/a.epub"), fake::real("/recents/a.epub"));
  fake_epub::metadata["/recents/a.epub"] = {"Alpha", ""};  // its reader cache moved with it
  ASSERT_TRUE(build());
  auto r = rows();
  ASSERT_EQ(r.size(), 2u);
  EXPECT_EQ(r[0].path, "/recents/a.epub");
  EXPECT_EQ(r[1].path, "/b.epub");

  fs::create_directories(fake::real("/read"));
  fs::rename(fake::real("/recents/a.epub"), fake::real("/read/a.epub"));
  fake_epub::metadata["/read/a.epub"] = {"Alpha", ""};
  ASSERT_TRUE(build());
  r = rows();
  ASSERT_EQ(r.size(), 2u);
  for (const auto& row : r) EXPECT_TRUE(fs::exists(fake::real(row.path))) << row.path << " no longer exists";
  EXPECT_EQ(r[0].path, "/read/a.epub");

  // Deleted: gone from the list.
  fs::remove(fake::real("/b.epub"));
  ASSERT_TRUE(build());
  EXPECT_EQ(rows().size(), 1u);
}

TEST_F(LibraryIndexTest, DropsADirectoryEntryTheCardListsTwice) {
  add("/dup.epub", "Dup");
  add("/other.epub", "Other");
  fake::duplicates["/"] = {"dup.epub"};
  BuildStats stats;
  ASSERT_TRUE(build(stats));
  EXPECT_EQ(stats.duplicates, 1);
  EXPECT_EQ(rows().size(), 2u);
}

TEST_F(LibraryIndexTest, SkipsPathsTooLongToStore) {
  add("/ok.epub", "Ok");
  const std::string deep = "/" + std::string(200, 'd') + "/" + std::string(100, 'f') + ".epub";
  add(deep, "Too long");
  BuildStats stats;
  ASSERT_TRUE(build(stats));
  EXPECT_EQ(stats.skipped, 1);
  EXPECT_EQ(rows().size(), 1u);
}

// --- the index file is never half-written ----------------------------------------------------

TEST_F(LibraryIndexTest, AFailedInstallLeavesTheOldIndexOrNoneNeverAHalfOne) {
  add("/a.epub", "Alpha");
  ASSERT_TRUE(build());
  add("/b.epub", "Beta");

  fake::failRenameCount = 0;  // the rename of the new index into place, after the old one is removed
  EXPECT_FALSE(build());
  const auto r = rows();  // empty when there is no index
  EXPECT_TRUE(r.empty() || (r.size() == 1 && r[0].title == "Alpha"));
  EXPECT_FALSE(fs::exists(fake::real(FsHelpers::partialPathFor(libraryIndexPath()))));

  ASSERT_TRUE(build());  // the next build recovers
  EXPECT_EQ(rows().size(), 2u);
}

TEST_F(LibraryIndexTest, AWriteThatDiesMidIndexLeavesThePreviousIndex) {
  for (int i = 0; i < 40; i++) add("/b" + std::to_string(i) + ".epub", "Book " + std::to_string(i));
  ASSERT_TRUE(build());
  add("/extra.epub", "Extra");
  // The stage file takes 513 * 41 bytes; die inside the index that follows it.
  fake::failWriteBytes = 1000;
  EXPECT_FALSE(build());
  fake::failWriteBytes = -1;
  EXPECT_EQ(rows().size(), 40u);
  EXPECT_FALSE(fs::exists(fake::real(FsHelpers::partialPathFor(libraryIndexPath()))));
}

TEST_F(LibraryIndexTest, ATruncatedIndexOrALeftoverPartIsNeverTrusted) {
  add("/a.epub", "Alpha");
  add("/b.epub", "Beta");
  ASSERT_TRUE(build());

  // A reset mid-build leaves "<index>.part" behind: not the final name, so never opened.
  const std::string part = fake::real(FsHelpers::partialPathFor(libraryIndexPath()));
  fs::copy_file(fake::real(libraryIndexPath()), part);
  fs::resize_file(part, 700);
  EXPECT_EQ(rows().size(), 2u);
  ASSERT_TRUE(build());  // and the next build simply overwrites it
  EXPECT_FALSE(fs::exists(part));

  // The final name itself cut short is rejected by its own header.
  fs::resize_file(fake::real(libraryIndexPath()), fs::file_size(fake::real(libraryIndexPath())) - 1);
  LibraryIndexFile index;
  EXPECT_FALSE(index.open(libraryIndexPath()));
  EXPECT_EQ(index.validity(), ClixValidity::SizeMismatch);
}

TEST_F(LibraryIndexTest, AnEmptyCardMakesAnEmptyIndex) {
  BuildStats stats;
  ASSERT_TRUE(build(stats));
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(libraryIndexPath()));
  EXPECT_EQ(index.bookCount(), 0);
}

TEST_F(LibraryIndexTest, CorruptRecordTextIsRefusedNotRead) {
  add("/a.epub", "Alpha");
  ASSERT_TRUE(build());
  {
    // Point the only record's text far outside the name section.
    std::fstream f(fake::real(libraryIndexPath()), std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(512 + offsetof(ClixRecord, nameOff));
    const uint32_t bad = 0x7FFFFFFF;
    f.write(reinterpret_cast<const char*>(&bad), sizeof(bad));
  }
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(libraryIndexPath()));
  ClixRecord r;
  EXPECT_FALSE(index.readRecord(0, r));
}

// --- scale: a 1000-book card ------------------------------------------------------------------

TEST_F(LibraryIndexTest, ThousandBooksPageInBoundedMemory) {
  constexpr int N = 1000;
  for (int i = 0; i < N; i++) {
    char title[64];
    std::snprintf(title, sizeof(title), "Book %04d", (i * 37) % N);  // not in order on disk
    add("/shelf" + std::to_string(i % 20) + "/file" + std::to_string(i) + ".epub", title, "Some Author Name");
  }

  BuildStats stats;
  const size_t base = meter::current.load();
  meter::peak = base;
  ASSERT_TRUE(build(stats));
  const size_t firstBuildPeak = meter::peak - base;
  EXPECT_EQ(stats.books, N);
  EXPECT_FALSE(stats.dedupDegraded);

  meter::peak = meter::current.load();
  ASSERT_TRUE(build(stats));  // a refresh: nothing changed, everything reused
  const size_t refreshPeak = meter::peak - base;
  EXPECT_EQ(stats.reused, N);
  EXPECT_EQ(stats.parsed, 0);

  // One page of the list: what the Library screen holds while drawing 24 rows.
  meter::peak = meter::current.load();
  {
    LibraryIndexFile index;
    ASSERT_TRUE(index.open(libraryIndexPath()));
    ClixRecord records[24];
    std::string titles[24], authors[24];
    ASSERT_EQ(index.readRecords(500, 24, records), 24u);
    for (int i = 0; i < 24; i++) {
      ASSERT_TRUE(index.readTitle(records[i], titles[i]));
      ASSERT_TRUE(index.readAuthor(records[i], authors[i]));
    }
    EXPECT_EQ(titles[0], "Book 0500");
    EXPECT_EQ(titles[23], "Book 0523");
  }
  const size_t pagePeak = meter::peak - base;

  const auto r = rows();
  ASSERT_EQ(r.size(), static_cast<size_t>(N));
  for (int i = 0; i < N; i++) {
    char want[16];
    std::snprintf(want, sizeof(want), "Book %04d", i);
    ASSERT_EQ(r[i].title, want);
  }
  for (const auto& row : r) ASSERT_TRUE(fs::exists(fake::real(row.path)));

  const size_t indexBytes = fs::file_size(fake::real(libraryIndexPath()));
  std::printf(
      "[library 1000 books] first build peak %zu B, refresh peak %zu B, one 24-row page %zu B, index file %zu B\n",
      firstBuildPeak, refreshPeak, pagePeak, indexBytes);
  // Host numbers are 64-bit (pointers and std::string are larger than on the ESP32): an upper
  // bound for the device. The resident part is ~42 B per book plus the walk's fixed buffers.
  EXPECT_LT(firstBuildPeak, 64u * 1024);
  EXPECT_LT(refreshPeak, 96u * 1024);
  EXPECT_LT(pagePeak, 16u * 1024);
}

TEST_F(LibraryIndexTest, PastTheCapTheWalkStopsAndSaysSo) {
  for (int i = 0; i < CLIX_MAX_RECORDS + 5; i++) add("/s" + std::to_string(i % 40) + "/b" + std::to_string(i) + ".txt");
  BuildStats stats;
  ASSERT_TRUE(build(stats));
  EXPECT_EQ(stats.books, CLIX_MAX_RECORDS);
  EXPECT_TRUE(stats.truncated);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(libraryIndexPath()));
  EXPECT_EQ(index.bookCount(), CLIX_MAX_RECORDS);
  EXPECT_NE(index.header().flags & CLIX_FLAG_TRUNCATED, 0);
}

TEST_F(LibraryIndexTest, DisablingMetadataSkipsEpubParsingAndRebuildsOnModeSwitch) {
  add("/books/file_name_a.epub", "Real Title A", "Real Author A");
  add("/books/file_name_b.epub", "Real Title B", "Real Author B");

  BuildStats stats;
  ASSERT_TRUE(buildLibraryIndex("/", libraryIndexPath(), stats, nullptr, nullptr, /*useMetadata=*/false));
  EXPECT_EQ(stats.parsed, 0u);
  EXPECT_EQ(fake_epub::parses, 0);
  {
    LibraryIndexFile index;
    ASSERT_TRUE(index.open(libraryIndexPath()));
    EXPECT_FALSE(index.metadataEnabled());
  }
  auto r = rows();
  ASSERT_EQ(r.size(), 2u);
  EXPECT_EQ(r[0].title, "file_name_a");
  EXPECT_EQ(r[0].author, "");
  EXPECT_EQ(r[1].title, "file_name_b");
  EXPECT_EQ(r[1].author, "");

  // Switching metadata back on must ignore the filename-only prior index and parse EPUBs.
  ASSERT_TRUE(buildLibraryIndex("/", libraryIndexPath(), stats, nullptr, nullptr, /*useMetadata=*/true));
  EXPECT_EQ(stats.reused, 0u);
  EXPECT_EQ(stats.parsed, 2u);
  {
    LibraryIndexFile index;
    ASSERT_TRUE(index.open(libraryIndexPath()));
    EXPECT_TRUE(index.metadataEnabled());
  }
  r = rows();
  ASSERT_EQ(r.size(), 2u);
  EXPECT_EQ(r[0].title, "Real Title A");
  EXPECT_EQ(r[0].author, "Real Author A");

  // Switching metadata back off must strip cached metadata instead of reusing it.
  fake_epub::parses = 0;
  ASSERT_TRUE(buildLibraryIndex("/", libraryIndexPath(), stats, nullptr, nullptr, /*useMetadata=*/false));
  EXPECT_EQ(stats.reused, 0u);
  EXPECT_EQ(fake_epub::parses, 0);
  r = rows();
  ASSERT_EQ(r.size(), 2u);
  EXPECT_EQ(r[0].title, "file_name_a");
  EXPECT_EQ(r[0].author, "");
}
