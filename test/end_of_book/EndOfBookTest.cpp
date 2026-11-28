#include <gtest/gtest.h>

#include "util/EndOfBookSuggestions.h"

namespace {

using namespace end_of_book;

TEST(EndOfBookSuggestionsTest, SameAuthorMatchesCaseInsensitiveAndTrimmed) {
  EXPECT_TRUE(isSameAuthor("Frank Herbert", "frank herbert"));
  EXPECT_TRUE(isSameAuthor("  Frank Herbert  ", "FRANK HERBERT"));
  EXPECT_FALSE(isSameAuthor("", ""));
  EXPECT_FALSE(isSameAuthor("   ", "Frank Herbert"));
  EXPECT_FALSE(isSameAuthor("Frank Herbert", "Brian Herbert"));
}

TEST(EndOfBookSuggestionsTest, SeriesPrefixStripsVolumeMarkersAndDigits) {
  EXPECT_EQ(extractSeriesPrefix("/books/Dune 01.epub"), "dune");
  EXPECT_EQ(extractSeriesPrefix("/recents/Dune - Book 1.epub"), "dune");
  EXPECT_EQ(extractSeriesPrefix("/books/Mistborn_Vol_02.epub"), "mistborn");
  EXPECT_EQ(extractSeriesPrefix("/books/Foundation Part 3.txt"), "foundation");
}

TEST(EndOfBookSuggestionsTest, FilenamePrefixMatchesSeriesWithoutFalsePositives) {
  EXPECT_TRUE(matchesFilenamePrefix("/recents/Dune 1.epub", "/books/Dune 2.epub"));
  EXPECT_TRUE(matchesFilenamePrefix("/recents/Dune 01.epub", "/books/Dune - Messiah.epub"));
  EXPECT_TRUE(matchesFilenamePrefix("/books/Foundation_1.epub", "/other/Foundation_2.epub"));
  // Must not match a different word sharing letters without a boundary.
  EXPECT_FALSE(matchesFilenamePrefix("/recents/Dune 1.epub", "/books/Dunes of Mars.epub"));
  // Short prefixes (< 4 chars) are ignored to avoid noisy matches like "The 1".
  EXPECT_FALSE(matchesFilenamePrefix("/recents/The 1.epub", "/books/The 2.epub"));
}

TEST(EndOfBookSuggestionsTest, LibraryFallbackTriggersOnRecentsOrEmptySiblings) {
  EXPECT_TRUE(shouldUseLibraryFallback("/recents/Dune 1.epub", 3));
  EXPECT_TRUE(shouldUseLibraryFallback("/books/Dune 1.epub", 0));
  EXPECT_FALSE(shouldUseLibraryFallback("/books/Dune 1.epub", 1));
}

TEST(EndOfBookSuggestionsTest, SkipLibraryEntryFiltersSelfReadFolderAndMovedDuplicate) {
  EXPECT_TRUE(shouldSkipLibraryEntry("/recents/Dune 1.epub", "/recents/Dune 1.epub"));
  // Stale index entry at original folder before filing into /recents.
  EXPECT_TRUE(shouldSkipLibraryEntry("/recents/Dune 1.epub", "/books/Dune 1.epub"));
  // Books already filed into /read are finished.
  EXPECT_TRUE(shouldSkipLibraryEntry("/recents/Dune 1.epub", "/read/Dune 2.epub"));
  EXPECT_FALSE(shouldSkipLibraryEntry("/recents/Dune 1.epub", "/books/Dune 2.epub"));
}

TEST(EndOfBookSuggestionsTest, InsertCandidatePrioritizesAuthorOverPrefixAndKeepsNaturalOrder) {
  std::vector<LibraryCandidate> picks;
  insertCandidate(picks, {"Dune 10", "/books/Dune 10.epub", 1});
  insertCandidate(picks, {"Dune 2", "/books/Dune 2.epub", 1});
  insertCandidate(picks, {"Children of Dune", "/books/Children of Dune.epub", 0});
  insertCandidate(picks, {"Dune Messiah", "/books/Dune Messiah.epub", 0});
  insertCandidate(picks, {"God Emperor", "/books/God Emperor.epub", 0});

  ASSERT_EQ(picks.size(), 3u);
  EXPECT_EQ(picks[0].displayName, "Children of Dune");
  EXPECT_EQ(picks[1].displayName, "Dune Messiah");
  EXPECT_EQ(picks[2].displayName, "God Emperor");
}

TEST(EndOfBookSuggestionsTest, InsertCandidateOrdersNumberedVolumesNaturally) {
  std::vector<LibraryCandidate> picks;
  insertCandidate(picks, {"Dune 10", "/books/Dune 10.epub", 1});
  insertCandidate(picks, {"Dune 3", "/books/Dune 3.epub", 1});
  insertCandidate(picks, {"Dune 2", "/books/Dune 2.epub", 1});
  insertCandidate(picks, {"Dune 4", "/books/Dune 4.epub", 1});

  ASSERT_EQ(picks.size(), 3u);
  EXPECT_EQ(picks[0].displayName, "Dune 2");
  EXPECT_EQ(picks[1].displayName, "Dune 3");
  EXPECT_EQ(picks[2].displayName, "Dune 4");
}

TEST(EndOfBookSuggestionsTest, ActionRowsAlwaysIncludeHomeEvenWithoutSuggestions) {
  const ActionRows bare = buildActionRows(0, false, false);
  ASSERT_EQ(bare.count, 1);
  EXPECT_EQ(bare[0].kind, RowKind::Home);

  const ActionRows full = buildActionRows(0, true, true);
  ASSERT_EQ(full.count, 3);
  EXPECT_EQ(full[0].kind, RowKind::SyncProgress);
  EXPECT_EQ(full[1].kind, RowKind::ViewQuotes);
  EXPECT_EQ(full[2].kind, RowKind::Home);
}

TEST(EndOfBookSuggestionsTest, ActionRowsPlaceBooksBeforeSyncQuotesAndHome) {
  const ActionRows rows = buildActionRows(2, true, true);
  ASSERT_EQ(rows.count, 5);
  EXPECT_EQ(rows[0].kind, RowKind::Book);
  EXPECT_EQ(rows[0].bookIndex, 0);
  EXPECT_EQ(rows[1].kind, RowKind::Book);
  EXPECT_EQ(rows[1].bookIndex, 1);
  EXPECT_EQ(rows[2].kind, RowKind::SyncProgress);
  EXPECT_EQ(rows[3].kind, RowKind::ViewQuotes);
  EXPECT_EQ(rows[4].kind, RowKind::Home);
}

}  // namespace
