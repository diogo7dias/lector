#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "util/BookSearch.h"

namespace {

using namespace book_search;

TEST(BookSearchTest, StripsTagsSkipsNonVisibleAndMatchesCaseInsensitive) {
  const std::string html =
      "<html><head><title>Whale</title><style>.whale{color:black}</style></head>"
      "<body><script>var whale = 1;</script>"
      "<p>Call me <em>Ishmael</em>. Some years ago, a great <b title=\"a>b\">WHALE</b> appeared.</p>"
      "<!-- hidden whale comment -->"
      "<p><ruby>漢<rp>(whale)</rp><rt>kan</rt></ruby></p>"
      "</body></html>";

  HtmlStreamScanner scanner("whale", 32);
  scanner.beginChapter(4);
  scanner.feed(html.data(), html.size());
  scanner.finishChapter();

  const auto& hits = scanner.hits();
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].spineIndex, 4);
  // Visible body text: "Call me Ishmael. Some years ago, a great WHALE appeared."
  // "WHALE" starts at codepoint index 41.
  EXPECT_EQ(hits[0].offset, 41u);
  EXPECT_NE(std::string(hits[0].snippet).find("WHALE"), std::string::npos);
  EXPECT_LT(std::strlen(hits[0].snippet), SNIPPET_BYTES);
}

TEST(BookSearchTest, DecodesNamedAndNumericEntitiesAndCountsCodepoints) {
  // &mdash; -> U+2014 (1 codepoint), &#39; -> ' (1 codepoint), &#xE9; -> é (1 codepoint)
  const std::string html = "<body><p>caf&#xE9;&mdash;it&#39;s a test</p></body>";

  HtmlStreamScanner scanner("test", 32);
  scanner.beginChapter(1);
  scanner.feed(html.data(), html.size());
  scanner.finishChapter();

  ASSERT_EQ(scanner.hits().size(), 1u);
  // "café—it's a " = 12 codepoints -> "test" starts at visibleTextOffset 12.
  EXPECT_EQ(scanner.hits()[0].offset, 12u);

  HtmlStreamScanner accent("caf\xC3\xA9", 32);
  accent.beginChapter(1);
  accent.feed(html.data(), html.size());
  accent.finishChapter();
  ASSERT_EQ(accent.hits().size(), 1u);
  EXPECT_EQ(accent.hits()[0].offset, 0u);
}

TEST(BookSearchTest, MatchesAcross2KbRingWraparoundAndChunkSplits) {
  std::string html = "<body><p>";
  html.append(RING_SIZE + 500, 'a');
  const uint32_t expectedOffset = static_cast<uint32_t>(RING_SIZE + 500);
  html += "needle in haystack";
  html.append(RING_SIZE + 100, 'b');
  html += "</p></body>";

  HtmlStreamScanner scanner("NEEDLE", 32);
  scanner.beginChapter(2);
  // Feed in tiny 17-byte chunks to stress state persistence across stream chunk boundaries.
  for (size_t pos = 0; pos < html.size(); pos += 17) {
    scanner.feed(html.data() + pos, std::min<size_t>(17, html.size() - pos));
  }
  scanner.finishChapter();

  ASSERT_EQ(scanner.hits().size(), 1u);
  EXPECT_EQ(scanner.hits()[0].offset, expectedOffset);
  EXPECT_NE(std::string(scanner.hits()[0].snippet).find("needle"), std::string::npos);
}

TEST(BookSearchTest, EnforcesHitCapAndReportsTruncated) {
  std::string html = "<body><p>";
  for (int i = 0; i < 50; ++i) html += "echo ";
  html += "</p></body>";

  HtmlStreamScanner scanner("echo", 5);
  scanner.beginChapter(0);
  scanner.feed(html.data(), html.size());
  scanner.finishChapter();

  EXPECT_EQ(scanner.hits().size(), 5u);
  EXPECT_TRUE(scanner.isFull());
}

TEST(BookSearchTest, BlockTagsSeparateWordsWithoutGluingAcrossParagraphs) {
  const std::string html = "<body><p>alpha</p><p>beta</p></body>";

  HtmlStreamScanner glued("alphabeta", 32);
  glued.beginChapter(0);
  glued.feed(html.data(), html.size());
  glued.finishChapter();
  EXPECT_TRUE(glued.hits().empty());

  HtmlStreamScanner spaced("alpha beta", 32);
  spaced.beginChapter(0);
  spaced.feed(html.data(), html.size());
  spaced.finishChapter();
  ASSERT_EQ(spaced.hits().size(), 1u);
  EXPECT_EQ(spaced.hits()[0].offset, 0u);
}

TEST(BookSearchTest, SnippetRespectsUtf8BoundariesAndByteCap) {
  // Multi-byte UTF-8 around match to verify no split codepoints at start or end of 48-byte snippet.
  const std::string html =
      "<body><p>\xE2\x80\x9C\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9 "
      "TARGET "
      "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xE2\x80\x9D</p></body>";

  HtmlStreamScanner scanner("target", 32);
  scanner.beginChapter(0);
  scanner.feed(html.data(), html.size());
  scanner.finishChapter();

  ASSERT_EQ(scanner.hits().size(), 1u);
  const std::string snip = scanner.hits()[0].snippet;
  EXPECT_LT(snip.size(), SNIPPET_BYTES);
  EXPECT_NE(snip.find("TARGET"), std::string::npos);
  // First byte must not be a UTF-8 continuation byte.
  ASSERT_FALSE(snip.empty());
  EXPECT_FALSE(isUtf8Continuation(static_cast<unsigned char>(snip.front())));
  // Walk UTF-8 sequences to prove valid ending boundary.
  size_t i = 0;
  while (i < snip.size()) {
    const unsigned char c = static_cast<unsigned char>(snip[i]);
    size_t step = 1;
    if ((c & 0xE0) == 0xC0)
      step = 2;
    else if ((c & 0xF0) == 0xE0)
      step = 3;
    else if ((c & 0xF8) == 0xF0)
      step = 4;
    ASSERT_LE(i + step, snip.size());
    for (size_t k = 1; k < step; ++k) EXPECT_TRUE(isUtf8Continuation(static_cast<unsigned char>(snip[i + k])));
    i += step;
  }
}

TEST(BookSearchTest, TxtStreamScannerTracksExactByteOffsets) {
  const std::string text = "Line one.\nLine two has Treasure inside.\nLine three has another treasure.\n";

  TxtStreamScanner scanner("treasure", 32);
  scanner.feed(text.data(), 20);
  scanner.feed(text.data() + 20, text.size() - 20);
  scanner.finish();

  ASSERT_EQ(scanner.hits().size(), 2u);
  EXPECT_EQ(scanner.hits()[0].offset, text.find("Treasure"));
  EXPECT_EQ(scanner.hits()[1].offset, text.find("treasure"));
}

}  // namespace
