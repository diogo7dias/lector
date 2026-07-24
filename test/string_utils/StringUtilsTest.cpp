// Host tests for StringUtils::authorInitials (used by the home in-progress list).
#include <gtest/gtest.h>

#include "StringUtils.h"

using StringUtils::authorInitials;

TEST(AuthorInitials, MultiWordUppercased) { EXPECT_EQ("UKLG", authorInitials("Ursula K. Le Guin")); }

TEST(AuthorInitials, DottedFirstNameIsOneWord) {
  // Split on whitespace only: "J.R.R." is a single word -> one initial.
  EXPECT_EQ("JT", authorInitials("J.R.R. Tolkien"));
}

TEST(AuthorInitials, LowercaseFirstLetterUppercased) { EXPECT_EQ("T", authorInitials("tolkien")); }

TEST(AuthorInitials, SingleWord) { EXPECT_EQ("P", authorInitials("Plato")); }

TEST(AuthorInitials, Empty) {
  EXPECT_EQ("", authorInitials(""));
  EXPECT_EQ("", authorInitials("   "));
}

TEST(AuthorInitials, CappedAtFour) { EXPECT_EQ("ABCD", authorInitials("Aa Bb Cc Dd Ee Ff")); }
