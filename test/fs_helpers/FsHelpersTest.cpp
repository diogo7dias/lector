#include <gtest/gtest.h>

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
