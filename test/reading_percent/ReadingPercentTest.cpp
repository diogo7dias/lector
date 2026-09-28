#include <gtest/gtest.h>

#include "ReadingPercent.h"

using reading_percent::pagePercent;

TEST(ReadingPercent, FirstPageIsZero) { EXPECT_EQ(pagePercent(0, 300), 0); }

TEST(ReadingPercent, LastPageIsHundredAndOnlyTheLast) {
  EXPECT_EQ(pagePercent(299, 300), 100);
  EXPECT_EQ(pagePercent(298, 300), 99);
  EXPECT_EQ(pagePercent(1, 2), 100);
}

TEST(ReadingPercent, SinglePageBookIsReadOnItsPage) { EXPECT_EQ(pagePercent(0, 1), 100); }

TEST(ReadingPercent, EmptyBookIsZero) { EXPECT_EQ(pagePercent(0, 0), 0); }

// XTC's "end of book" screen sits one past the last page; it still reads 100.
TEST(ReadingPercent, PastTheEndClampsToHundred) { EXPECT_EQ(pagePercent(300, 300), 100); }

// EPUB's byte-weighted fraction can land a hair under 1 on the last page.
TEST(ReadingPercent, FractionJustUnderOneStillReadsHundred) {
  EXPECT_EQ(reading_percent::toPercent(0.99999994f), 100);
  EXPECT_EQ(reading_percent::toPercent(0.995f), 99);
}
