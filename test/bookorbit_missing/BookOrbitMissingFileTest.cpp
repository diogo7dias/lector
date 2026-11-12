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

TEST(BookOrbitMissingFile, DueEverySixHoursOrWhenWifiIsUp) {
  constexpr long long t = 1790000000;
  EXPECT_TRUE(due(true, 0, t));    // a push already joined
  EXPECT_FALSE(due(false, 0, 0));  // clock never set
  EXPECT_TRUE(due(false, t, 0));   // never checked
  EXPECT_FALSE(due(false, t + CHECK_EVERY_S - 1, t));
  EXPECT_TRUE(due(false, t + CHECK_EVERY_S, t));
  EXPECT_TRUE(due(false, t - 10, t));  // clock went back
}
