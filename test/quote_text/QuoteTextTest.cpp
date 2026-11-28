// Host tests for the pure Grab Quote helpers: sidecar path derivation, the
// word-join punctuation rule, the size guard, and the on-disk entry format.
// The interactive selection + SD write are device-only and covered on hardware.
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "GrowthBounds.h"
#include "util/QuoteText.h"

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

TEST(QuoteText, AppendSuppressesSpaceBeforePunctuation) {
  const std::vector<std::string> words = {"Hello", ",", "world", "!", "Again"};
  std::string out;
  for (const auto& word : words) {
    EXPECT_TRUE(appendQuoteWord(out, word.c_str()));
  }
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
  EXPECT_EQ("\f[Ch 1]\nHello world\n---\n\n", formatQuoteEntry("Ch 1", "", "Hello world"));
}

TEST(QuoteText, EveryEntryOpensWithAPageBreak) {
  // The sidecar read as a book must start each quote on its own page.
  EXPECT_EQ(PAGE_BREAK, formatQuoteEntry("Ch 1", "", "Hello")[0]);
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

// Both sidecar readers (the viewer and the reader's underline index) walk records
// through nextQuoteRecord, so its idea of the grammar is the file's.
TEST(QuoteText, NextQuoteRecordWalksWrittenEntries) {
  const std::string buf =
      formatQuoteEntry("Ch 1", "@q1:1,2,3", "first") + formatQuoteEntry("Ch 2", "", "second") + "\n\nbare tail";
  size_t pos = 0;
  QuoteRecord rec;
  std::vector<std::string> headers, texts;
  while (nextQuoteRecord(buf, pos, rec)) {
    headers.push_back(rec.hasHeader ? buf.substr(rec.headerStart, rec.headerLen) : "<none>");
    texts.push_back(buf.substr(rec.textStart, rec.textEnd - rec.textStart));
  }
  EXPECT_EQ((std::vector<std::string>{"Ch 1 @q1:1,2,3", "Ch 2", "<none>"}), headers);
  EXPECT_EQ((std::vector<std::string>{"first", "second", "bare tail"}), texts);
  EXPECT_EQ(buf.size(), pos);
}

TEST(QuoteText, NextQuoteRecordUnclosedBracketIsText) {
  const std::string buf = "[no close\nquote\n---\n";
  size_t pos = 0;
  QuoteRecord rec;
  ASSERT_TRUE(nextQuoteRecord(buf, pos, rec));
  EXPECT_FALSE(rec.hasHeader);
  EXPECT_EQ("[no close\nquote", buf.substr(rec.textStart, rec.textEnd - rec.textStart));
  EXPECT_FALSE(nextQuoteRecord(buf, pos, rec));
}

TEST(QuoteText, MetaRoundTripsAndOldReadersKeepTheAnchor) {
  QuoteMeta meta;
  meta.savedAt = 1763590000u;
  meta.textOffset = 4200;
  const std::string entry = formatQuoteEntry("Ch @t: 1", "@q1:3,7,2", "quote", meta);
  EXPECT_EQ("\f[Ch @t: 1 @o:4200 @t:1763590000 @q1:3,7,2]\nquote\n---\n\n", entry);
  std::string chapter, anchor;
  QuoteMeta back;
  splitChapterAnchor("Ch @t: 1 @o:4200 @t:1763590000 @q1:3,7,2", chapter, anchor, &back);
  EXPECT_EQ("Ch @t: 1", chapter);  // a title that merely looks like a token stays whole
  EXPECT_EQ("@q1:3,7,2", anchor);
  EXPECT_EQ(1763590000u, back.savedAt);
  EXPECT_EQ(4200u, back.textOffset);
  // A reader that only knows anchors still finds this one (old firmware, the reader's underline).
  splitChapterAnchor("Ch @t: 1 @o:4200 @t:1763590000 @q1:3,7,2", chapter, anchor);
  EXPECT_EQ("@q1:3,7,2", anchor);
  // Time only, no anchor; and a record from before any of this.
  splitChapterAnchor("Ch @t:5", chapter, anchor, &back);
  EXPECT_EQ("Ch", chapter);
  EXPECT_EQ(5u, back.savedAt);
  EXPECT_EQ(QuoteMeta::NO_OFFSET, back.textOffset);
  splitChapterAnchor("Ch @q1:3,7,2", chapter, anchor, &back);
  EXPECT_EQ("Ch", chapter);
  EXPECT_EQ(0u, back.savedAt);
  // An entry without meta is byte for byte what it always was.
  EXPECT_EQ("\f[Ch @q1:3,7,2]\nquote\n---\n\n", formatQuoteEntry("Ch", "@q1:3,7,2", "quote"));
}

TEST(QuoteText, KoreaderDatetimeAndStableFallback) {
  EXPECT_EQ("2026-11-19 22:05:07", koreaderDatetime(1795125907));
  EXPECT_EQ(fallbackSavedAt("same words"), fallbackSavedAt("same words"));
  EXPECT_NE(fallbackSavedAt("same words"), fallbackSavedAt("other words"));
  EXPECT_GE(fallbackSavedAt("x"), 1577836800u);
}

TEST(QuoteText, KindleClippingLayout) {
  EXPECT_EQ(
      "Dune (Frank Herbert)\n- Your Highlight on Page 12 | Book One | Added on Thursday, November 19, 2026, 10:05 PM"
      "\n\nFear is the mind-killer.\n==========\n",
      formatClipping("Dune", "Frank Herbert", "Book One", 12, "Fear is the mind-killer.", 1795125907));
  EXPECT_EQ("Dune (Frank Herbert)\n- Your Highlight on Page 1\n\nq\n==========\n",
            formatClipping("Dune", "Frank Herbert", "", 1, "q", 0));
}

TEST(QuoteText, NoteRoundTripsInSidecarEntry) {
  QuoteMeta meta;
  meta.savedAt = 1763590000u;
  meta.textOffset = 4200;
  const std::string entry = formatQuoteEntry("Ch 1", "@q1:3,7,2", "Fear is the mind-killer.", meta, "Key litany");
  EXPECT_EQ("\f[Ch 1 @o:4200 @t:1763590000 @q1:3,7,2]\nFear is the mind-killer.\n> Key litany\n---\n\n", entry);

  size_t pos = 0;
  QuoteRecord rec;
  ASSERT_TRUE(nextQuoteRecord(entry, pos, rec));
  EXPECT_TRUE(rec.hasHeader);
  EXPECT_EQ("Fear is the mind-killer.", entry.substr(rec.textStart, rec.textEnd - rec.textStart));
  EXPECT_TRUE(rec.hasNote);
  EXPECT_EQ("Key litany", entry.substr(rec.noteStart, rec.noteEnd - rec.noteStart));
  EXPECT_FALSE(nextQuoteRecord(entry, pos, rec));
}

TEST(QuoteText, NoteSanitizesNewlinesAndCapsAtMaxLength) {
  EXPECT_EQ("line one line two", sanitizeNote("  line one\r\nline two \n "));
  EXPECT_EQ("", sanitizeNote("   \n\r\t  "));
  const std::string longNote(MAX_NOTE_LENGTH + 40, 'x');
  EXPECT_EQ(MAX_NOTE_LENGTH, sanitizeNote(longNote).size());
  // Whitespace-only note produces identical byte-for-byte sidecar entry without "> ".
  EXPECT_EQ(formatQuoteEntry("Ch 1", "", "quote"), formatQuoteEntry("Ch 1", "", "quote", {}, "   \n"));
}

TEST(QuoteText, LegacyEntryWithoutNoteReportsNoNote) {
  const std::string entry = formatQuoteEntry("Ch 1", "@q1:1,2,3", "Plain quote");
  size_t pos = 0;
  QuoteRecord rec;
  ASSERT_TRUE(nextQuoteRecord(entry, pos, rec));
  EXPECT_FALSE(rec.hasNote);
  EXPECT_EQ("Plain quote", entry.substr(rec.textStart, rec.textEnd - rec.textStart));
}

TEST(QuoteText, KindleNoteClippingLayout) {
  EXPECT_EQ(
      "Dune (Frank Herbert)\n- Your Note on Page 12 | Book One | Added on Thursday, November 19, 2026, 10:05 PM"
      "\n\nKey litany\n==========\n",
      formatNoteClipping("Dune", "Frank Herbert", "Book One", 12, "Key litany", 1795125907));
  EXPECT_EQ("Dune (Frank Herbert)\n- Your Note on Page 1\n\nn\n==========\n",
            formatNoteClipping("Dune", "Frank Herbert", "", 1, "n", 0));
}
