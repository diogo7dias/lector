// Host tests for the quote-underline recovery logic: finding the saved quote's
// words again on a laid-out page, and where the line sits inside a line box.
// The drawing itself is device-only.
#include <gtest/gtest.h>

#include <vector>

#include "QuoteUnderline.h"

using namespace quote_underline;

namespace {

// Page tokens as the reader sees them: NUL-terminated pointers into a TextBlock.
bool find(const std::vector<const char*>& words, const char* quote, size_t hint, size_t& first, size_t& last) {
  return findQuoteRun(words.data(), words.size(), quote, hint, first, last);
}

}  // namespace

TEST(QuoteUnderline, FindsRunInMiddleOfPage) {
  const std::vector<const char*> words = {"the", "quick", "brown", "fox", "jumped"};
  size_t first = 99, last = 99;
  EXPECT_TRUE(find(words, "quick brown fox", 0, first, last));
  EXPECT_EQ(1u, first);
  EXPECT_EQ(3u, last);
}

TEST(QuoteUnderline, FindsRunAtPageStartAndEnd) {
  const std::vector<const char*> words = {"alpha", "beta", "gamma"};
  size_t first = 99, last = 99;
  EXPECT_TRUE(find(words, "alpha beta", 0, first, last));
  EXPECT_EQ(0u, first);
  EXPECT_EQ(1u, last);
  EXPECT_TRUE(find(words, "gamma", 0, first, last));
  EXPECT_EQ(2u, first);
  EXPECT_EQ(2u, last);
}

TEST(QuoteUnderline, SingleWordQuote) {
  const std::vector<const char*> words = {"alpha", "beta"};
  size_t first = 99, last = 99;
  EXPECT_TRUE(find(words, "beta", 0, first, last));
  EXPECT_EQ(1u, first);
  EXPECT_EQ(1u, last);
}

TEST(QuoteUnderline, PunctuationTokensMatchJoinedQuote) {
  // The page keeps punctuation as its own token; joinQuoteWords glued it on.
  const std::vector<const char*> words = {"Hello", ",", "world", "!"};
  size_t first = 99, last = 99;
  EXPECT_TRUE(find(words, "Hello, world!", 0, first, last));
  EXPECT_EQ(0u, first);
  EXPECT_EQ(3u, last);
}

TEST(QuoteUnderline, HyphenSplitPageStillMatchesUnbrokenQuote) {
  // Relayout hyphenated the word after the quote was saved.
  const std::vector<const char*> words = {"an", "exam-", "ple", "sentence"};
  size_t first = 99, last = 99;
  EXPECT_TRUE(find(words, "example sentence", 0, first, last));
  EXPECT_EQ(1u, first);
  EXPECT_EQ(3u, last);
}

TEST(QuoteUnderline, HyphenSplitQuoteStillMatchesUnbrokenPage) {
  // The other direction: quote grabbed while hyphenated, page now is not.
  const std::vector<const char*> words = {"an", "example", "sentence"};
  size_t first = 99, last = 99;
  EXPECT_TRUE(find(words, "exam- ple sentence", 0, first, last));
  EXPECT_EQ(1u, first);
  EXPECT_EQ(2u, last);
}

TEST(QuoteUnderline, SoftHyphenIsIgnored) {
  const std::vector<const char*> words = {"exam\xC2\xAD", "ple"};
  size_t first = 99, last = 99;
  EXPECT_TRUE(find(words, "example", 0, first, last));
  EXPECT_EQ(0u, first);
  EXPECT_EQ(1u, last);
}

TEST(QuoteUnderline, NoMatchLeavesOutputsUntouched) {
  const std::vector<const char*> words = {"alpha", "beta"};
  size_t first = 42, last = 43;
  EXPECT_FALSE(find(words, "gamma delta", 0, first, last));
  EXPECT_EQ(42u, first);
  EXPECT_EQ(43u, last);
}

TEST(QuoteUnderline, QuoteRunningPastPageEndUnderlinesWhatIsVisible) {
  // The quote carries on onto the next page. The part on this page is still drawn;
  // the rest is picked up by findQuoteContinuation when that page renders.
  const std::vector<const char*> words = {"alpha", "beta"};
  size_t first = 99, last = 99;
  EXPECT_TRUE(find(words, "beta gamma", 0, first, last));
  EXPECT_EQ(1u, first);
  EXPECT_EQ(1u, last);
}

TEST(QuoteUnderline, PartialWordDoesNotMatch) {
  // "bet" is a prefix of the token "beta" — underlining half a word is wrong.
  const std::vector<const char*> words = {"alpha", "beta"};
  size_t first = 99, last = 99;
  EXPECT_FALSE(find(words, "alpha bet", 0, first, last));
}

TEST(QuoteUnderline, RepeatedPassageResolvesNearestTheHint) {
  const std::vector<const char*> words = {"say", "it", "again", "say", "it", "again"};
  size_t first = 99, last = 99;
  EXPECT_TRUE(find(words, "say it", 0, first, last));
  EXPECT_EQ(0u, first);
  EXPECT_TRUE(find(words, "say it", 3, first, last));
  EXPECT_EQ(3u, first);
  EXPECT_EQ(4u, last);
}

TEST(QuoteUnderline, SearchWrapsWhenHintIsPastTheMatch) {
  const std::vector<const char*> words = {"alpha", "beta", "gamma"};
  size_t first = 99, last = 99;
  EXPECT_TRUE(find(words, "alpha beta", 2, first, last));
  EXPECT_EQ(0u, first);
  EXPECT_EQ(1u, last);
}

TEST(QuoteUnderline, OutOfRangeHintIsClamped) {
  const std::vector<const char*> words = {"alpha", "beta"};
  size_t first = 99, last = 99;
  EXPECT_TRUE(find(words, "beta", 999, first, last));
  EXPECT_EQ(1u, first);
}

TEST(QuoteUnderline, EmptyInputsAreRejected) {
  const std::vector<const char*> words = {"alpha"};
  size_t first = 99, last = 99;
  EXPECT_FALSE(find(words, "", 0, first, last));
  EXPECT_FALSE(find(words, "   ", 0, first, last));
  EXPECT_FALSE(findQuoteRun(nullptr, 0, "alpha", 0, first, last));
  EXPECT_FALSE(findQuoteRun(words.data(), 0, "alpha", 0, first, last));
}

TEST(QuoteUnderline, LeadingHyphenRemainderIsNotMatchedFailSafe) {
  // Known limit, pinned deliberately: a quote whose FIRST token is the tail of a
  // hyphen split cannot be located once the page stops hyphenating. It draws
  // nothing rather than guessing.
  const std::vector<const char*> words = {"example", "sentence"};
  size_t first = 99, last = 99;
  EXPECT_FALSE(find(words, "ple sentence", 0, first, last));
}

TEST(QuoteUnderline, ThicknessGrowsWithLineHeight) {
  EXPECT_EQ(1, underlineThickness(20));
  EXPECT_EQ(1, underlineThickness(27));
  EXPECT_EQ(2, underlineThickness(28));
  EXPECT_EQ(2, underlineThickness(64));
}

TEST(QuoteUnderline, UnderlineStaysInsideItsLineBox) {
  for (int lineHeight = 14; lineHeight <= 64; lineHeight++) {
    const int thickness = underlineThickness(lineHeight);
    const int ascender = lineHeight - 2;  // worst case: glyphs fill the box
    const int y = underlineY(100, ascender, lineHeight, thickness);
    EXPECT_GE(y, 100) << "lineHeight " << lineHeight;
    EXPECT_LE(y + thickness, 100 + lineHeight) << "lineHeight " << lineHeight;
  }
}

TEST(QuoteUnderline, UnderlineSitsBelowTheGlyphs) { EXPECT_GT(underlineY(0, 20, 30, 1), 20); }

// --- Quotes that run across a page boundary -------------------------------

TEST(QuoteUnderline, HeadOfAQuoteThatContinuesOnTheNextPageIsUnderlined) {
  // Page one ends mid-quote. Everything from the start word to the last word of
  // the page is covered.
  const std::vector<const char*> words = {"before", "the", "quick", "brown"};
  size_t first = 99, last = 99;
  EXPECT_TRUE(find(words, "the quick brown fox jumped", 0, first, last));
  EXPECT_EQ(1u, first);
  EXPECT_EQ(3u, last);
}

TEST(QuoteUnderline, RestOfTheQuoteIsUnderlinedOnTheFollowingPage) {
  const std::vector<const char*> words = {"fox", "jumped", "over", "later", "words"};
  size_t last = 99;
  EXPECT_TRUE(findQuoteContinuation(words.data(), words.size(), "the quick brown fox jumped", last));
  EXPECT_EQ(1u, last);
}

TEST(QuoteUnderline, ContinuationCoveringTheWholePageIsAccepted) {
  // A quote spanning three pages: this middle page is entirely inside it.
  const std::vector<const char*> words = {"brown", "fox"};
  size_t last = 99;
  EXPECT_TRUE(findQuoteContinuation(words.data(), words.size(), "the quick brown fox jumped over", last));
  EXPECT_EQ(1u, last);
}

TEST(QuoteUnderline, ContinuationPrefersTheLongerAgreement) {
  // "over" appears twice in the quote. The occurrence that carries on matching
  // wins over the one that stops after a single word.
  const std::vector<const char*> words = {"over", "the", "hill"};
  size_t last = 99;
  EXPECT_TRUE(findQuoteContinuation(words.data(), words.size(), "jumped over and over the hill", last));
  EXPECT_EQ(2u, last);
}

TEST(QuoteUnderline, PageUnrelatedToTheQuoteIsNotAContinuation) {
  const std::vector<const char*> words = {"nothing", "in", "common"};
  size_t last = 99;
  EXPECT_FALSE(findQuoteContinuation(words.data(), words.size(), "the quick brown fox", last));
  EXPECT_EQ(99u, last);
}

TEST(QuoteUnderline, ContinuationRejectsEmptyInputs) {
  const std::vector<const char*> words = {"alpha"};
  size_t last = 99;
  EXPECT_FALSE(findQuoteContinuation(nullptr, 0, "alpha", last));
  EXPECT_FALSE(findQuoteContinuation(words.data(), 0, "alpha", last));
  EXPECT_FALSE(findQuoteContinuation(words.data(), words.size(), "", last));
}
