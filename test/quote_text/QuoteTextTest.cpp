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

TEST(QuoteText, AppendBuildsTheSameStringAsJoin) {
  // A quote picked across page ends is built one word at a time; it must come out
  // byte-identical to the same words joined in one go on a single page.
  const std::vector<std::string> words = {"Hello", ",", "world", "!", "Again"};
  std::string out;
  for (const auto& word : words) {
    EXPECT_TRUE(appendQuoteWord(out, word.c_str()));
  }
  EXPECT_EQ(joinQuoteWords(words), out);
  EXPECT_EQ("Hello, world! Again", out);
}

TEST(QuoteText, AppendPutsNoSpaceBeforeTheFirstWord) {
  std::string out;
  EXPECT_TRUE(appendQuoteWord(out, "First"));
  EXPECT_EQ("First", out);
}

TEST(QuoteText, AppendRefusesPastTheCapAndLeavesTheTextIntact) {
  std::string out = "abc";
  EXPECT_FALSE(appendQuoteWord(out, "de", 5));  // "abc" + " " + "de" = 6 > 5
  EXPECT_EQ("abc", out);
  EXPECT_TRUE(appendQuoteWord(out, ".", 5));  // punctuation attaches: no space, fits
  EXPECT_EQ("abc.", out);
}

TEST(QuoteText, AppendFillsExactlyToTheCap) {
  std::string out = "abc";
  EXPECT_TRUE(appendQuoteWord(out, "de", 6));
  EXPECT_EQ("abc de", out);
}

TEST(QuoteText, AppendRejectsNull) {
  std::string out = "abc";
  EXPECT_FALSE(appendQuoteWord(out, nullptr));
  EXPECT_EQ("abc", out);
}

TEST(QuoteText, EntryFormatMatchesSidecarLayout) {
  EXPECT_EQ("\f[Ch 1]\nHello world\n---\n\n", formatQuoteEntry("Ch 1", "Hello world"));
}

TEST(QuoteText, EveryEntryOpensWithAPageBreak) {
  // The sidecar read as a book must start each quote on its own page.
  EXPECT_EQ(PAGE_BREAK, formatQuoteEntry("Ch 1", "Hello")[0]);
  EXPECT_EQ(PAGE_BREAK, formatQuoteEntry("Ch 1", "@q1:1,2,3", "Hello")[0]);
}

TEST(QuoteText, RecordGapCoversThePageBreak) {
  EXPECT_TRUE(isRecordGap('\n'));
  EXPECT_TRUE(isRecordGap('\r'));
  EXPECT_TRUE(isRecordGap(' '));
  EXPECT_TRUE(isRecordGap(PAGE_BREAK));
  EXPECT_FALSE(isRecordGap('['));
  EXPECT_FALSE(isRecordGap('a'));
}

TEST(QuoteText, AnchorTokenRoundTrips) {
  QuoteAnchor anchor;
  anchor.spine = 12;
  anchor.paragraph = 37;
  anchor.wordHint = 84;
  anchor.valid = true;
  const std::string token = formatAnchorToken(anchor);
  EXPECT_EQ("@q1:12,37,84", token);

  QuoteAnchor parsed;
  ASSERT_TRUE(parseAnchorToken(token, parsed));
  EXPECT_TRUE(parsed.valid);
  EXPECT_EQ(12, parsed.spine);
  EXPECT_EQ(37, parsed.paragraph);
  EXPECT_EQ(84, parsed.wordHint);
}

TEST(QuoteText, InvalidAnchorTokenIsRejected) {
  QuoteAnchor parsed;
  EXPECT_FALSE(parseAnchorToken("", parsed));
  EXPECT_FALSE(parseAnchorToken("@q2:1,2,3", parsed));      // unknown version
  EXPECT_FALSE(parseAnchorToken("@q1:1,2", parsed));        // missing field
  EXPECT_FALSE(parseAnchorToken("@q1:1,2,3,4", parsed));    // trailing junk
  EXPECT_FALSE(parseAnchorToken("@q1:a,2,3", parsed));      // not digits
  EXPECT_FALSE(parseAnchorToken("@q1:1,,3", parsed));       // empty field
  EXPECT_FALSE(parseAnchorToken("@q1:70000,2,3", parsed));  // will not fit uint16
  EXPECT_FALSE(parsed.valid);
}

TEST(QuoteText, InvalidAnchorIsNotFormatted) {
  QuoteAnchor anchor;  // valid defaults to false
  EXPECT_EQ("", formatAnchorToken(anchor));
}

TEST(QuoteText, SplitChapterAnchorSeparatesTitleAndToken) {
  std::string chapter, token;
  splitChapterAnchor("Chapter One @q1:12,37,84", chapter, token);
  EXPECT_EQ("Chapter One", chapter);
  EXPECT_EQ("@q1:12,37,84", token);
}

TEST(QuoteText, SplitChapterAnchorLeavesPlainTitleAlone) {
  std::string chapter, token;
  splitChapterAnchor("Chapter One", chapter, token);
  EXPECT_EQ("Chapter One", chapter);
  EXPECT_EQ("", token);

  splitChapterAnchor("An email @q1:not-an-anchor", chapter, token);
  EXPECT_EQ("An email @q1:not-an-anchor", chapter);
  EXPECT_EQ("", token);
}

TEST(QuoteText, EntryWithAnchorKeepsRecordGrammar) {
  EXPECT_EQ("\f[Ch 1 @q1:2,5,7]\nHello world\n---\n\n", formatQuoteEntry("Ch 1", "@q1:2,5,7", "Hello world"));
}

TEST(QuoteText, EntryWithEmptyAnchorWritesNoToken) {
  EXPECT_EQ(formatQuoteEntry("Ch 1", "Hello world"), formatQuoteEntry("Ch 1", "", "Hello world"));
}

TEST(QuoteText, AnchoredEntryRoundTripsThroughSplit) {
  QuoteAnchor anchor;
  anchor.spine = 3;
  anchor.paragraph = 0;  // paragraph unknown on the grab page
  anchor.wordHint = 11;
  anchor.valid = true;
  const std::string field = "Ch 2 " + formatAnchorToken(anchor);

  std::string chapter, token;
  splitChapterAnchor(field, chapter, token);
  EXPECT_EQ("Ch 2", chapter);
  QuoteAnchor parsed;
  ASSERT_TRUE(parseAnchorToken(token, parsed));
  EXPECT_EQ(3, parsed.spine);
  EXPECT_EQ(0, parsed.paragraph);
  EXPECT_EQ(11, parsed.wordHint);
}

TEST(GrowthBounds, WithinAndOverLimit) {
  EXPECT_TRUE(memory::canGrowWithinLimit(0, 100, 1000));
  EXPECT_TRUE(memory::canGrowWithinLimit(900, 100, 1000));
  EXPECT_FALSE(memory::canGrowWithinLimit(900, 101, 1000));
  EXPECT_FALSE(memory::canGrowWithinLimit(1001, 0, 1000));
}
