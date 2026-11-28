#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "util/ClippingsReader.h"

namespace {

size_t readFromString(void* ctx, const uint64_t offset, char* dst, const size_t len) {
  const auto* s = static_cast<const std::string*>(ctx);
  if (offset >= s->size()) return 0;
  const size_t avail = std::min<size_t>(len, static_cast<size_t>(s->size() - offset));
  std::memcpy(dst, s->data() + offset, avail);
  return avail;
}

TEST(ClippingsReader, ParsesTitleAuthorAndMultilineQuote) {
  const std::string raw =
      "\xEF\xBB\xBF"
      "The Left Hand of Darkness (Ursula K. Le Guin)\r\n"
      "- Your Highlight on Page 42 | Chapter 3 | Added on Monday, Nov 1, 2026\r\n"
      "\r\n"
      "Light is the left hand of darkness\r\n"
      "and darkness the right hand of light.\r\n";
  clippings::Record rec;
  ASSERT_TRUE(clippings::parseRecord(raw, rec));
  EXPECT_EQ(rec.title, "The Left Hand of Darkness");
  EXPECT_EQ(rec.author, "Ursula K. Le Guin");
  EXPECT_EQ(rec.quote, "Light is the left hand of darkness\r\nand darkness the right hand of light.");
}

TEST(ClippingsReader, HandlesTitleWithoutAuthorAndParenthesesInTitle) {
  const std::string noAuthor =
      "Meditations\n"
      "- Your Highlight on Page 1\n"
      "\n"
      "The obstacle is the way.\n";
  clippings::Record rec;
  ASSERT_TRUE(clippings::parseRecord(noAuthor, rec));
  EXPECT_EQ(rec.title, "Meditations");
  EXPECT_EQ(rec.author, "");
  EXPECT_EQ(rec.quote, "The obstacle is the way.");

  const std::string nested =
      "Dune (Chronicles) (Frank Herbert)\n"
      "- Your Highlight on Page 9\n"
      "\n"
      "Fear is the mind-killer.\n";
  ASSERT_TRUE(clippings::parseRecord(nested, rec));
  EXPECT_EQ(rec.title, "Dune (Chronicles)");
  EXPECT_EQ(rec.author, "Frank Herbert");
  EXPECT_EQ(rec.quote, "Fear is the mind-killer.");
}

TEST(ClippingsReader, RejectsEmptyBookmarkEntryAndEmptyFile) {
  const std::string bookmark =
      "Some Book (Author)\n"
      "- Your Bookmark on Page 10\n"
      "\n\n";
  clippings::Record rec;
  EXPECT_FALSE(clippings::parseRecord(bookmark, rec));

  std::string empty;
  EXPECT_FALSE(clippings::pickAtOffset(0, 0, readFromString, &empty, rec));
}

TEST(ClippingsReader, SeeksPastSeparatorAndWrapsToFirstRecord) {
  std::string file =
      "Book One (Author One)\n"
      "- Your Highlight on Page 1\n"
      "\n"
      "First quote.\n"
      "==========\n"
      "Bookmark Only (Author X)\n"
      "- Your Bookmark on Page 2\n"
      "\n"
      "==========\n"
      "Book Two (Author Two)\n"
      "- Your Highlight on Page 5\n"
      "\n"
      "Second quote.\n"
      "==========\n";

  clippings::Record rec;
  // Offset 0 gets the first record directly.
  ASSERT_TRUE(clippings::pickAtOffset(file.size(), 0, readFromString, &file, rec));
  EXPECT_EQ(rec.title, "Book One");
  EXPECT_EQ(rec.quote, "First quote.");

  // Offset inside Book One skips to next "==========", skips empty bookmark, lands on Book Two.
  ASSERT_TRUE(clippings::pickAtOffset(file.size(), 10, readFromString, &file, rec));
  EXPECT_EQ(rec.title, "Book Two");
  EXPECT_EQ(rec.quote, "Second quote.");

  // Offset near end of file wraps around to Book One.
  ASSERT_TRUE(clippings::pickAtOffset(file.size(), file.size() - 5, readFromString, &file, rec));
  EXPECT_EQ(rec.title, "Book One");
  EXPECT_EQ(rec.quote, "First quote.");
}

TEST(ClippingsReader, DailySeedIsDeterministicPerDayAndVariesAcrossDays) {
  const uint64_t day1a = clippings::dailySeed(2026, 11, 28);
  const uint64_t day1b = clippings::dailySeed(2026, 11, 28);
  const uint64_t day2 = clippings::dailySeed(2026, 11, 29);
  EXPECT_EQ(day1a, day1b);
  EXPECT_NE(day1a, day2);
}

}  // namespace
