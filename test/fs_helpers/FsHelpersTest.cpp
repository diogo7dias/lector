#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <string_view>

#include "FsHelpers.h"

TEST(FsHelpers, RejectsEmptyAndDotComponents) {
  EXPECT_FALSE(FsHelpers::isSafePathComponent(std::string_view("")));
  EXPECT_FALSE(FsHelpers::isSafePathComponent(std::string_view(".")));
  EXPECT_FALSE(FsHelpers::isSafePathComponent(std::string_view("..")));
}

TEST(FsHelpers, RejectsPathSeparators) {
  EXPECT_FALSE(FsHelpers::isSafePathComponent(std::string_view("a/b")));
  EXPECT_FALSE(FsHelpers::isSafePathComponent(std::string_view("a\\b")));
}

TEST(FsHelpers, AcceptsOrdinaryAndDottedNames) {
  EXPECT_TRUE(FsHelpers::isSafePathComponent(std::string_view("book.epub")));
  EXPECT_TRUE(FsHelpers::isSafePathComponent(std::string_view("volume..2.epub")));
  EXPECT_TRUE(FsHelpers::isSafePathComponent(std::string_view("notes...txt")));
}

// A partly received file is written under ".part" for one reason: nothing that
// lists books may see it. Every scanner on the device picks files by extension,
// so this is the property the whole scheme rests on.
TEST(FsHelpers, PartialFilesAreInvisibleToEveryBookScanner) {
  const std::string partialString = FsHelpers::partialPathFor("/books/Dune.epub");
  EXPECT_EQ(partialString, "/books/Dune.epub.part");
  const std::string_view partial{partialString};
  EXPECT_TRUE(FsHelpers::hasPartialExtension(partial));

  EXPECT_FALSE(FsHelpers::hasEpubExtension(partial));
  EXPECT_FALSE(FsHelpers::hasXtcExtension(partial));
  EXPECT_FALSE(FsHelpers::hasTxtExtension(partial));
  EXPECT_FALSE(FsHelpers::hasMarkdownExtension(partial));
  EXPECT_FALSE(FsHelpers::hasBmpExtension(partial));
  EXPECT_FALSE(FsHelpers::hasPngExtension(partial));
  EXPECT_FALSE(FsHelpers::checkFileExtension(partial, ".cpfont"));
  EXPECT_FALSE(FsHelpers::checkFileExtension(partial, ".bin"));

  // A font face on its way over is hidden from the registry the same way.
  const std::string facePartial = FsHelpers::partialPathFor("/.fonts/Literata/Literata_14.cpfont");
  EXPECT_TRUE(FsHelpers::hasPartialExtension(std::string_view{facePartial}));
  EXPECT_FALSE(FsHelpers::checkFileExtension(std::string_view{facePartial}, ".cpfont"));
}

TEST(FsHelpers, PartialNamesRoundTripBackToTheRealOne) {
  EXPECT_EQ(FsHelpers::finalPathForPartial("/books/Dune.epub.part"), "/books/Dune.epub");
  EXPECT_EQ(FsHelpers::finalPathForPartial(FsHelpers::partialPathFor("/A book (2).epub")), "/A book (2).epub");
  // Not a partial: left exactly as it was rather than trimmed on a guess.
  EXPECT_EQ(FsHelpers::finalPathForPartial("/books/Dune.epub"), "/books/Dune.epub");
  EXPECT_FALSE(FsHelpers::hasPartialExtension(std::string_view("/books/Dune.epub")));
  EXPECT_FALSE(FsHelpers::hasPartialExtension(std::string_view("/books/part")));
}

TEST(FsHelpers, NextSessionIndexFindsFirstGapAndClampsWhenFull) {
  const auto upTo = [](const int taken) { return [taken](const int index) { return index <= taken; }; };
  EXPECT_EQ(FsHelpers::nextSessionIndex(1, 50, upTo(0)), 1);    // empty card
  EXPECT_EQ(FsHelpers::nextSessionIndex(1, 50, upTo(17)), 18);  // 1..17 exist
  EXPECT_EQ(FsHelpers::nextSessionIndex(1, 50, upTo(50)), 50);  // full: reuse the last
  EXPECT_EQ(FsHelpers::nextSessionIndex(0, 199, upTo(-1)), 0);
  EXPECT_EQ(FsHelpers::nextSessionIndex(0, 199, upTo(198)), 199);
  EXPECT_EQ(FsHelpers::nextSessionIndex(0, 199, upTo(199)), 199);
}

// Both HTTP front doors (file API and WebDAV) resolve request paths through this,
// so ".." never climbs above the card root and every path has one spelling.
TEST(FsHelpers, NormaliseWebPathIsAbsoluteWithoutTrailingSlash) {
  EXPECT_EQ(FsHelpers::normaliseWebPath(""), "/");
  EXPECT_EQ(FsHelpers::normaliseWebPath("/"), "/");
  EXPECT_EQ(FsHelpers::normaliseWebPath("books/"), "/books");
  EXPECT_EQ(FsHelpers::normaliseWebPath("//books//a.epub/"), "/books/a.epub");
  EXPECT_EQ(FsHelpers::normaliseWebPath("/../../.crosspoint"), "/.crosspoint");
  EXPECT_EQ(FsHelpers::normaliseWebPath("/books/../x"), "/x");
}

namespace {
// ScreenshotUtil sanitizes into a 64-byte buffer.
std::string sanitize(const char* input, const size_t size = 64) {
  char out[64];
  FsHelpers::sanitizePathComponentForFat32(input, out, size);
  return out;
}

// Titles from the EPUBs in crosspoint-reader#2103 and #2199.
constexpr char kLongTitle[] = "Богиня глюкозы. Нормализуйте уровень сахара в крови, чтобы изменить свою жизнь";
constexpr char kShortTitle[] = "Вглядываясь в солнце. Жизнь без страха смерти";
}  // namespace

TEST(FsHelpers, SanitizeKeepsMultiByteTitleThatFits) {
  EXPECT_EQ(sanitize("Эдем (полный перевод)"), "Эдем-(полный-перевод)");
}

// The readers snprintf the title into ScreenshotInfo::title (char[64]), which can end
// the copy partway through a Cyrillic letter. FAT32 rejects the folder name if the
// half letter survives, and the screenshot is lost.
TEST(FsHelpers, SanitizeDropsLetterCutOffByCaller) {
  char title[64];
  snprintf(title, sizeof(title), "%s", kLongTitle);
  EXPECT_EQ(sanitize(title), "Богиня-глюкозы.-Нормализуйте-уров");
  snprintf(title, sizeof(title), "%s", kShortTitle);
  EXPECT_EQ(sanitize(title), "Вглядываясь-в-солнце.-Жизнь-без-ст");
}

TEST(FsHelpers, SanitizeDoesNotSplitLetterAtBufferLimit) {
  // Each letter is 2 bytes; 7 bytes of room hold "Жиз" and half of "н".
  EXPECT_EQ(sanitize("Жизнь", 8), "Жиз");
  EXPECT_EQ(sanitize(kLongTitle), "Богиня-глюкозы.-Нормализуйте-уров");
}
