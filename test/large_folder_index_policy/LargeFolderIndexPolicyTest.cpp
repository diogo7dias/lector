#include <FsHelpers.h>
#include <gtest/gtest.h>

#include <set>

#include "activities/home/LargeFolderIndexPolicy.h"
#include "activities/home/LibrarySearchSupport.h"

TEST(LargeFolderIndexPolicy, SwitchesBeforeNameStorageCanBecomeLarge) {
  EXPECT_TRUE(large_folder_index::shouldUseSdIndex(257));
  EXPECT_FALSE(large_folder_index::shouldUseSdIndex(256));
  EXPECT_TRUE(large_folder_index::shouldUseSdIndex(20, large_folder_index::IN_MEMORY_NAME_BYTES_LIMIT + 1));
}

TEST(LargeFolderIndexPolicy, RejectsIndexPastHardLimit) {
  EXPECT_TRUE(large_folder_index::canAddEntry(19999));
  EXPECT_FALSE(large_folder_index::canAddEntry(20000));
}

TEST(LargeFolderIndexPolicy, AffineShuffleIsPermutationAndKeepsDirectoriesFixed) {
  constexpr size_t firstFile = 3;
  constexpr size_t count = 20;
  std::set<size_t> mapped;
  for (size_t i = 0; i < count; ++i) {
    const size_t result = large_folder_index::mapShuffledIndex(i, count, firstFile, 5, 7);
    if (i < firstFile) EXPECT_EQ(result, i);
    mapped.insert(result);
  }
  EXPECT_EQ(mapped.size(), count);
}

TEST(LargeFolderIndexPolicy, ChoosesCoprimeShuffleMultiplier) {
  for (size_t count = 2; count < 100; ++count) {
    EXPECT_EQ(std::gcd(large_folder_index::coprimeMultiplier(42, count), count), 1U);
  }
}

TEST(LargeFolderIndexPolicy, SearchResultsStayBoundedForHugeFolder) {
  constexpr size_t count = 5000;
  const auto results = LibrarySearchSupport::rankMatches(
      count, [](const size_t index) { return "matching book " + std::to_string(index) + ".epub"; }, "book",
      large_folder_index::MAX_SEARCH_RESULTS);
  EXPECT_EQ(results.size(), large_folder_index::MAX_SEARCH_RESULTS);
}

TEST(LargeFolderIndexPolicy, NaturalSortHandlesBoundedViewsAndNumbers) {
  EXPECT_TRUE(FsHelpers::naturalFileLess(std::string_view("folder 2/"), std::string_view("folder 10/")));
  EXPECT_TRUE(FsHelpers::naturalFileLess(std::string_view("dir/"), std::string_view("book.epub")));
  EXPECT_FALSE(FsHelpers::naturalFileLess(std::string_view("book 10.epub"), std::string_view("book 2.epub")));
}
