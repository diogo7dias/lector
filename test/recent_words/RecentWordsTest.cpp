#include <RecentWordList.h>
#include <gtest/gtest.h>

namespace {

using Words = std::vector<std::string>;

TEST(RecentWords, PutsANewWordAtTheFront) {
  Words words{"beta", "gamma"};
  EXPECT_TRUE(recent_words::moveToFront(words, "alpha", 10));
  EXPECT_EQ(words, (Words{"alpha", "beta", "gamma"}));
}

TEST(RecentWords, MovesAKnownWordInsteadOfDuplicatingIt) {
  Words words{"beta", "gamma", "alpha"};
  EXPECT_TRUE(recent_words::moveToFront(words, "alpha", 10));
  EXPECT_EQ(words, (Words{"alpha", "beta", "gamma"}));
}

TEST(RecentWords, ReportsNoChangeWhenTheWordIsAlreadyNewest) {
  Words words{"alpha", "beta"};
  EXPECT_FALSE(recent_words::moveToFront(words, "alpha", 10));
  EXPECT_EQ(words, (Words{"alpha", "beta"}));
}

TEST(RecentWords, DropsTheOldestPastTheCap) {
  Words words{"b", "c", "d"};
  EXPECT_TRUE(recent_words::moveToFront(words, "a", 3));
  EXPECT_EQ(words, (Words{"a", "b", "c"}));
}

TEST(RecentWords, IgnoresAnEmptyWordAndAZeroCap) {
  Words words{"alpha"};
  EXPECT_FALSE(recent_words::moveToFront(words, "", 10));
  EXPECT_FALSE(recent_words::moveToFront(words, "beta", 0));
  EXPECT_EQ(words, (Words{"alpha"}));
}

}  // namespace
