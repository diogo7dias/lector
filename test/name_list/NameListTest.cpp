#include <NameList.h>
#include <gtest/gtest.h>

#include <string>

namespace {

std::string longName(const int i) { return "wallpaper_" + std::to_string(i) + "_1080x1440.pxc"; }

TEST(NameList, StoresAndReadsBackNames) {
  NameList list;
  ASSERT_TRUE(list.push("alpha.pxc"));
  ASSERT_TRUE(list.push("beta.bmp"));
  ASSERT_EQ(list.size(), 2u);
  EXPECT_EQ(list[0], "alpha.pxc");
  EXPECT_EQ(list[1], "beta.bmp");
  EXPECT_FALSE(list.truncated());
}

TEST(NameList, EntriesAreNulTerminatedForCApis) {
  NameList list;
  ASSERT_TRUE(list.push("cover.bmp"));
  ASSERT_TRUE(list.push("next.pxc"));
  EXPECT_STREQ(list.cstr(0), "cover.bmp");
  EXPECT_STREQ(list.cstr(1), "next.pxc");
}

TEST(NameList, HandlesEmptyAndSingleCharNames) {
  NameList list;
  ASSERT_TRUE(list.push(""));
  ASSERT_TRUE(list.push("a"));
  EXPECT_EQ(list[0], "");
  EXPECT_EQ(list[1], "a");
  EXPECT_EQ(list.size(), 2u);
}

// The crash this class exists to prevent: a folder with thousands of images must
// truncate, not abort and not grow without bound.
TEST(NameList, ThousandsOfImagesTruncateInsteadOfGrowingWithoutBound) {
  NameList list;
  int accepted = 0;
  for (int i = 0; i < 20000; i++) {
    if (list.push(longName(i))) accepted++;
  }
  EXPECT_TRUE(list.truncated());
  EXPECT_EQ(list.size(), static_cast<size_t>(accepted));
  EXPECT_LT(list.size(), 20000u);
  EXPECT_LE(list.size(), NameList::MAX_ENTRIES);
  EXPECT_GT(list.size(), 0u);
  // Everything accepted is still intact after all that growth.
  EXPECT_EQ(list[0], longName(0));
  EXPECT_EQ(list[list.size() - 1], longName(static_cast<int>(list.size()) - 1));
}

TEST(NameList, EntryCapIsNeverExceeded) {
  NameList list;
  for (uint32_t i = 0; i < NameList::MAX_ENTRIES + 50; i++) list.push("x");
  EXPECT_LE(list.size(), NameList::MAX_ENTRIES);
  EXPECT_TRUE(list.truncated());
}

TEST(NameList, SortByOrdersWithoutMovingCharacters) {
  NameList list;
  ASSERT_TRUE(list.push("charlie"));
  ASSERT_TRUE(list.push("alpha"));
  ASSERT_TRUE(list.push("bravo"));
  list.sortByC([](const char* a, const char* b) { return std::string_view(a) < std::string_view(b); });
  EXPECT_EQ(list[0], "alpha");
  EXPECT_EQ(list[1], "bravo");
  EXPECT_EQ(list[2], "charlie");
  EXPECT_STREQ(list.cstr(0), "alpha");
}

TEST(NameList, ShuffleTailLeavesTheHeadAlone) {
  NameList list;
  ASSERT_TRUE(list.push("folderA/"));
  ASSERT_TRUE(list.push("folderB/"));
  for (int i = 0; i < 20; i++) ASSERT_TRUE(list.push("file" + std::to_string(i)));
  uint32_t seed = 12345;
  list.shuffleTail(2, [&seed] { return seed = seed * 1103515245u + 12345u; });
  EXPECT_EQ(list[0], "folderA/");
  EXPECT_EQ(list[1], "folderB/");
  // Every original file name still present exactly once.
  for (int i = 0; i < 20; i++) {
    int seen = 0;
    for (size_t j = 2; j < list.size(); j++) {
      if (list[j] == "file" + std::to_string(i)) seen++;
    }
    EXPECT_EQ(seen, 1) << "file" << i;
  }
}

TEST(NameList, ShuffleTailIsSafeWhenTailIsTooShort) {
  NameList list;
  ASSERT_TRUE(list.push("only/"));
  uint32_t seed = 1;
  list.shuffleTail(1, [&seed] { return seed = seed * 1103515245u + 12345u; });
  list.shuffleTail(5, [&seed] { return seed = seed * 1103515245u + 12345u; });
  EXPECT_EQ(list.size(), 1u);
  EXPECT_EQ(list[0], "only/");
}

TEST(NameList, ClearResetsTruncationAndReuses) {
  NameList list;
  for (uint32_t i = 0; i < NameList::MAX_ENTRIES + 10; i++) list.push("y");
  ASSERT_TRUE(list.truncated());
  list.clear();
  EXPECT_EQ(list.size(), 0u);
  EXPECT_TRUE(list.empty());
  EXPECT_FALSE(list.truncated());
  ASSERT_TRUE(list.push("fresh.pxc"));
  EXPECT_EQ(list[0], "fresh.pxc");
}

}  // namespace
