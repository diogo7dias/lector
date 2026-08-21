#include <gtest/gtest.h>

#include "BookBadgeLabel.h"

TEST(BookBadgeLabel, NeverOpenedDrawsNoChip) { EXPECT_EQ(book_badge::chipLabel(-1, "Read"), ""); }

TEST(BookBadgeLabel, PartlyReadIsABracketedPercentage) {
  EXPECT_EQ(book_badge::chipLabel(42, "Read"), "[42%]");
  EXPECT_EQ(book_badge::chipLabel(0, "Read"), "[0%]");
  EXPECT_EQ(book_badge::chipLabel(99, "Read"), "[99%]");
}

TEST(BookBadgeLabel, FinishedReadsAsAWord) { EXPECT_EQ(book_badge::chipLabel(100, "Read"), "Read"); }

// A book reported past the end still reads as finished rather than as a number above 100.
TEST(BookBadgeLabel, PastTheEndStillReadsAsFinished) { EXPECT_EQ(book_badge::chipLabel(120, "Read"), "Read"); }

TEST(BookBadgeLabel, TheFinishedWordIsTranslated) { EXPECT_EQ(book_badge::chipLabel(100, "Lido"), "Lido"); }
