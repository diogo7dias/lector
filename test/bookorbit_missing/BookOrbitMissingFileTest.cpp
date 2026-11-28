#include <gtest/gtest.h>

#include "src/util/BookOrbitMissingFile.h"

using namespace bookorbit_missing_file;

TEST(BookOrbitMissingFile, ReadsCountStampAndUrl) {
  const Saved s = parse("2 1790000000\nhttps://h:8456/api/v1/opds/catalog?smartScopeId=5\n");
  EXPECT_EQ(s.count, 2);
  EXPECT_EQ(s.checkedAt, 1790000000LL);
  EXPECT_EQ(s.feedUrl, "https://h:8456/api/v1/opds/catalog?smartScopeId=5");
}

TEST(BookOrbitMissingFile, TornOrForeignFileMeansNothingMissing) {
  for (const char* text : {"", "2", "2 1790000000\n", "garbage\nurl\n", "-3 1790000000\nhttps://h\n"}) {
    const Saved s = parse(text);
    EXPECT_EQ(s.count, 0) << text;
    EXPECT_TRUE(s.feedUrl.empty()) << text;
  }
}

TEST(BookOrbitMissingFile, DueEverySixHoursOrWhenWifiIsUpOnlyAfterPagesMoved) {
  constexpr long long t = 1790000000;
  // Zero pages moved: never triggers Wi-Fi on lock, even past 6 hours or never checked.
  EXPECT_FALSE(due(false, false, t, 0));
  EXPECT_FALSE(due(false, false, t + CHECK_EVERY_S, t));
  EXPECT_FALSE(due(true, false, t, t));

  // Pages moved: piggybacks when Wi-Fi is up, otherwise honours the 6-hour interval.
  EXPECT_TRUE(due(true, true, 0, t));    // a push already joined
  EXPECT_FALSE(due(false, true, 0, 0));  // clock never set
  EXPECT_TRUE(due(false, true, t, 0));   // never checked
  EXPECT_FALSE(due(false, true, t + CHECK_EVERY_S - 1, t));
  EXPECT_TRUE(due(false, true, t + CHECK_EVERY_S, t));
  EXPECT_TRUE(due(false, true, t - 10, t));  // clock went back
}

// What the old sscanf("%d %lld\n%n") accepted, pinned so the parser can change: any run of
// whitespace between and after the two numbers, signs, and a URL right after the stamp.
TEST(BookOrbitMissingFile, ReadsTheNumbersTheWaySscanfDid) {
  const Saved spaced = parse("  +4 \t 17 \r\n\n https://h\r\n");
  EXPECT_EQ(spaced.count, 4);
  EXPECT_EQ(spaced.checkedAt, 17LL);
  EXPECT_EQ(spaced.feedUrl, "https://h");
  const Saved split = parse("4\n-17\nhttps://h");
  EXPECT_EQ(split.count, 4);
  EXPECT_EQ(split.checkedAt, -17LL);
  EXPECT_EQ(split.feedUrl, "https://h");
  EXPECT_EQ(parse("4 17https://h").feedUrl, "https://h");
  EXPECT_EQ(parse("4 9999999999\nhttps://h").checkedAt, 9999999999LL);
  EXPECT_TRUE(parse("4 x\nhttps://h").feedUrl.empty());
}
