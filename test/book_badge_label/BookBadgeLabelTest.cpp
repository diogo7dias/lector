#include <BookBadgeLabel.h>
#include <gtest/gtest.h>

namespace {

TEST(BookBadgeLabel, ShowsOnlyTheFileTypeForABookNeverOpened) {
  EXPECT_EQ(book_badge::label(-1, "EPUB", "Read"), "EPUB");
}

TEST(BookBadgeLabel, LeadsWithThePercentage) {
  EXPECT_EQ(book_badge::label(42, "EPUB", "Read"), "42%  EPUB");
  EXPECT_EQ(book_badge::label(0, "EPUB", "Read"), "0%  EPUB");
  EXPECT_EQ(book_badge::label(99, "EPUB", "Read"), "99%  EPUB");
}

TEST(BookBadgeLabel, SaysReadInsteadOfAHundredPercent) {
  EXPECT_EQ(book_badge::label(100, "EPUB", "Read"), "Read  EPUB");
}

TEST(BookBadgeLabel, EmitsNoSeparatorWhenThereIsNoFileType) {
  EXPECT_EQ(book_badge::label(42, "", "Read"), "42%");
  EXPECT_EQ(book_badge::label(100, "", "Read"), "Read");
  EXPECT_EQ(book_badge::label(-1, "", "Read"), "");
}

TEST(BookBadgeLabel, CarriesTheTranslatedWordThrough) {
  EXPECT_EQ(book_badge::label(100, "EPUB", "Lido"), "Lido  EPUB");
}

}  // namespace
