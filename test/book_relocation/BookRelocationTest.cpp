#include <gtest/gtest.h>

#include "src/BookRelocation.h"

using namespace book_relocation;

TEST(BookRelocation, BaseNameHandlesNoSlash) {
  EXPECT_EQ(baseName("/a/b/c.txt"), "c.txt");
  EXPECT_EQ(baseName("c.txt"), "c.txt");
  EXPECT_EQ(baseName("/recents/Dune.epub"), "Dune.epub");
}

TEST(BookRelocation, DestPathKeepsFilenameUnderRecents) {
  EXPECT_EQ(recentsDestPath("/books/scifi/Dune.epub"), "/recents/Dune.epub");
  EXPECT_EQ(recentsDestPath("Dune.epub"), "/recents/Dune.epub");
}

TEST(BookRelocation, UnderRecentsIsCaseInsensitive) {
  EXPECT_TRUE(isUnderRecents("/recents/Dune.epub"));
  EXPECT_TRUE(isUnderRecents("/Recents/Dune.epub"));
  EXPECT_FALSE(isUnderRecents("/books/Dune.epub"));
  // Must be the /recents/ root, not just any folder containing the word.
  EXPECT_FALSE(isUnderRecents("/my_recents/Dune.epub"));
}

TEST(BookRelocation, QuotesSidecarStripsExtension) {
  EXPECT_EQ(quotesSidecarPath("/books/Dune.epub"), "/books/Dune_QUOTES.txt");
  EXPECT_EQ(quotesSidecarPath("/recents/Dune.epub"), "/recents/Dune_QUOTES.txt");
  EXPECT_EQ(quotesSidecarPath("/books/no_ext"), "/books/no_ext_QUOTES.txt");
}
