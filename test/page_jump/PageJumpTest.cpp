#include <gtest/gtest.h>

#include "activities/reader/PageJump.h"

namespace {

TEST(PageJump, ParsesValidOneBasedPageToZeroBasedIndex) {
  EXPECT_EQ(page_jump::parseChapterPage("1", 10), 0);
  EXPECT_EQ(page_jump::parseChapterPage("5", 10), 4);
  EXPECT_EQ(page_jump::parseChapterPage("10", 10), 9);
  EXPECT_EQ(page_jump::parseChapterPage("  7\t", 10), 6);
  EXPECT_EQ(page_jump::parseChapterPage("01", 10), 0);
}

TEST(PageJump, RejectsOutOfRangeOrMalformedInput) {
  EXPECT_EQ(page_jump::parseChapterPage("", 10), std::nullopt);
  EXPECT_EQ(page_jump::parseChapterPage("   ", 10), std::nullopt);
  EXPECT_EQ(page_jump::parseChapterPage("0", 10), std::nullopt);
  EXPECT_EQ(page_jump::parseChapterPage("-1", 10), std::nullopt);
  EXPECT_EQ(page_jump::parseChapterPage("+2", 10), std::nullopt);
  EXPECT_EQ(page_jump::parseChapterPage("11", 10), std::nullopt);
  EXPECT_EQ(page_jump::parseChapterPage("3a", 10), std::nullopt);
  EXPECT_EQ(page_jump::parseChapterPage("xii", 10), std::nullopt);
  EXPECT_EQ(page_jump::parseChapterPage("999999999999", 10), std::nullopt);
  EXPECT_EQ(page_jump::parseChapterPage("1", 0), std::nullopt);
  EXPECT_EQ(page_jump::parseChapterPage("1", -5), std::nullopt);
}

TEST(PageJump, FormatsChapterTitleWithCurrentAndTotal) {
  char buf[48];
  page_jump::formatChapterTitle(buf, sizeof(buf), "Go to Page", 3, 18);
  EXPECT_STREQ(buf, "Go to Page (3/18)");

  page_jump::formatChapterTitle(buf, sizeof(buf), "Go to Page", 0, 18);
  EXPECT_STREQ(buf, "Go to Page (1/18)");

  page_jump::formatChapterTitle(buf, sizeof(buf), "Go to Page", 25, 18);
  EXPECT_STREQ(buf, "Go to Page (18/18)");

  page_jump::formatChapterTitle(buf, sizeof(buf), "Go to Page", 1, 0);
  EXPECT_STREQ(buf, "Go to Page");
}

}  // namespace
