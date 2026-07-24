// Host tests for the pure Grab Quote helpers: sidecar path derivation, the
// word-join punctuation rule, the size guard, and the on-disk entry format.
// The interactive selection + SD write are device-only and covered on hardware.
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "GrowthBounds.h"
#include "QuoteText.h"

using namespace quote_text;

TEST(QuoteText, FilePathStripsLastExtension) {
  EXPECT_EQ("/books/War_QUOTES.txt", quotesFilePathFor("/books/War.epub"));
  EXPECT_EQ("/books/War_QUOTES.txt", quotesFilePathFor("/books/War.txt"));
}

TEST(QuoteText, FilePathNoExtensionAppends) { EXPECT_EQ("/books/War_QUOTES.txt", quotesFilePathFor("/books/War")); }

TEST(QuoteText, WordAttachesLeftForClosingPunctuation) {
  EXPECT_TRUE(wordAttachesLeft(","));
  EXPECT_TRUE(wordAttachesLeft("."));
  EXPECT_TRUE(wordAttachesLeft("!"));
  EXPECT_TRUE(wordAttachesLeft(")"));
  EXPECT_TRUE(wordAttachesLeft("\""));
  EXPECT_FALSE(wordAttachesLeft("word"));
  EXPECT_FALSE(wordAttachesLeft("("));
  EXPECT_FALSE(wordAttachesLeft(""));
  EXPECT_FALSE(wordAttachesLeft(nullptr));
}

TEST(QuoteText, JoinSuppressesSpaceBeforePunctuation) {
  EXPECT_EQ("Hello, world!", joinQuoteWords({"Hello", ",", "world", "!"}));
}

TEST(QuoteText, JoinPlainWordsGetSingleSpaces) { EXPECT_EQ("a b c", joinQuoteWords({"a", "b", "c"})); }

TEST(QuoteText, JoinHardCapsLength) {
  const std::vector<std::string> many(1000, "xxxx");
  EXPECT_LE(joinQuoteWords(many, 100).size(), 100u);
}

TEST(QuoteText, EntryFormatMatchesSidecarLayout) {
  EXPECT_EQ("[Ch 1]\nHello world\n---\n\n", formatQuoteEntry("Ch 1", "Hello world"));
}

TEST(GrowthBounds, WithinAndOverLimit) {
  EXPECT_TRUE(memory::canGrowWithinLimit(0, 100, 1000));
  EXPECT_TRUE(memory::canGrowWithinLimit(900, 100, 1000));
  EXPECT_FALSE(memory::canGrowWithinLimit(900, 101, 1000));
  EXPECT_FALSE(memory::canGrowWithinLimit(1001, 0, 1000));
}
