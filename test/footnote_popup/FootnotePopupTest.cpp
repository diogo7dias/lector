#include <Epub.h>
#include <Epub/FootnoteEntry.h>
#include <Epub/FootnoteExtractor.h>
#include <Epub/Page.h>
#include <Epub/parsers/ChapterHtmlSlimParser.h>
#include <FootnoteHitTest.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

ReaderRenderSpec defaultSpec() {
  ReaderRenderSpec spec;
  spec.fontId = 1;
  spec.lineCompression = 1.1f;
  spec.viewportWidth = 456;
  spec.viewportHeight = 760;
  spec.imageRendering = 0;
  return spec;
}

std::string collectPageWords(const Page& page, std::vector<std::string>* outWords = nullptr) {
  std::string joined;
  for (const auto& el : page.elements) {
    if (el->getTag() != TAG_PageLine) continue;
    const auto* line = static_cast<const PageLine*>(el.get());
    const auto& block = line->getBlock();
    if (!block || !block->valid()) continue;
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      if (!joined.empty()) joined.push_back(' ');
      joined.append(block->wordText(i));
      if (outWords) outWords->emplace_back(block->wordText(i));
    }
  }
  return joined;
}

}  // namespace

TEST(FootnoteExtractor, SplitHrefHandlesFragmentFileAndPercentEncoding) {
  std::string file;
  std::string frag;

  EXPECT_TRUE(footnote_extract::splitHref("#fn1", file, frag));
  EXPECT_EQ(file, "");
  EXPECT_EQ(frag, "fn1");

  EXPECT_TRUE(footnote_extract::splitHref("notes.xhtml#note%3A2", file, frag));
  EXPECT_EQ(file, "notes.xhtml");
  EXPECT_EQ(frag, "note:2");

  EXPECT_FALSE(footnote_extract::splitHref("chapter2.xhtml", file, frag));
  EXPECT_EQ(file, "chapter2.xhtml");
  EXPECT_TRUE(frag.empty());

  EXPECT_FALSE(footnote_extract::splitHref("notes.xhtml#", file, frag));
  EXPECT_EQ(file, "notes.xhtml");
  EXPECT_TRUE(frag.empty());
}

TEST(FootnoteExtractor, DetectsFootnoteAsideElements) {
  const char* id = nullptr;
  const char* epubFootnote[] = {"id", "fn1", "epub:type", "footnote", nullptr};
  EXPECT_TRUE(footnote_extract::isFootnoteAsideElement("aside", epubFootnote, &id));
  EXPECT_STREQ(id, "fn1");

  const char* ariaFootnote[] = {"xml:id", "n2", "role", "doc-footnote", nullptr};
  EXPECT_TRUE(footnote_extract::isFootnoteAsideElement("ASIDE", ariaFootnote, &id));
  EXPECT_STREQ(id, "n2");

  const char* rearnote[] = {"id", "r3", "epub:type", "note rearnote", nullptr};
  EXPECT_TRUE(footnote_extract::isFootnoteAsideElement("aside", rearnote, &id));
  EXPECT_STREQ(id, "r3");

  // Regular sidebar <aside> or unlinked <aside> without id stays in main flow.
  const char* sidebar[] = {"id", "box1", "class", "sidebar", nullptr};
  EXPECT_FALSE(footnote_extract::isFootnoteAsideElement("aside", sidebar, &id));

  const char* noId[] = {"epub:type", "footnote", nullptr};
  EXPECT_FALSE(footnote_extract::isFootnoteAsideElement("aside", noId, &id));

  const char* divFootnote[] = {"id", "fn1", "epub:type", "footnote", nullptr};
  EXPECT_FALSE(footnote_extract::isFootnoteAsideElement("div", divFootnote, &id));
}

TEST(FootnoteExtractor, ExtractsAsideWithEntitiesAndParagraphs) {
  const char* html =
      "<html><body>"
      "<p>Main body<a href=\"#fn1\">1</a>.</p>"
      "<aside id=\"fn1\" epub:type=\"footnote\">"
      "<style>.hide{display:none}</style>"
      "<p>First &amp; foremost &#x2014; a <em>quoted</em> note.</p>"
      "<p>Second paragraph&nbsp;here&#33;</p>"
      "</aside>"
      "<p>After aside.</p>"
      "</body></html>";

  footnote_extract::FootnoteTextExtractor ex("fn1");
  EXPECT_FALSE(ex.feed(html, std::strlen(html)));
  EXPECT_TRUE(ex.found());
  EXPECT_FALSE(ex.truncated());
  EXPECT_TRUE(ex.isFootnoteAside());
  EXPECT_EQ(ex.text(), "First & foremost \xE2\x80\x94 a quoted note.\nSecond paragraph\xC2\xA0here!");
}

TEST(FootnoteExtractor, PromotesInlineAnchorToEnclosingParagraph) {
  const char* html =
      "<html><body>"
      "<p><a id=\"fn2\" href=\"#ref2\">[2]</a> Note attached via inline anchor.</p>"
      "<p>Unrelated next note.</p>"
      "</body></html>";

  footnote_extract::FootnoteTextExtractor ex("fn2");
  EXPECT_FALSE(ex.feed(html, std::strlen(html)));
  EXPECT_TRUE(ex.found());
  EXPECT_FALSE(ex.isFootnoteAside());
  EXPECT_EQ(ex.text(), "[2] Note attached via inline anchor.");

  // Self-closing empty anchor inside <li>
  const char* htmlSelfClose = "<ul><li><a id=\"fn3\"/>Text after empty anchor.</li><li>Other</li></ul>";
  footnote_extract::FootnoteTextExtractor ex2("fn3");
  EXPECT_FALSE(ex2.feed(htmlSelfClose, std::strlen(htmlSelfClose)));
  EXPECT_TRUE(ex2.found());
  EXPECT_EQ(ex2.text(), "Text after empty anchor.");
}

TEST(FootnoteExtractor, StreamsAcrossTinyChunksAndCapsUtf8Safely) {
  std::string body(2500, 'a');
  // Put a 3-byte UTF-8 em-dash right near the 64-byte cap boundary.
  const std::string html = "<div id=\"long\"><p>" + std::string(62, 'x') + "\xE2\x80\x94" + body + "</p></div>";

  footnote_extract::FootnoteTextExtractor ex("long", 64);
  bool keepGoing = true;
  for (size_t i = 0; i < html.size() && keepGoing; i += 3) {
    const size_t n = std::min<size_t>(3, html.size() - i);
    keepGoing = ex.feed(html.data() + i, n);
  }
  EXPECT_FALSE(keepGoing);
  EXPECT_TRUE(ex.found());
  EXPECT_TRUE(ex.truncated());
  EXPECT_LE(ex.text().size(), 64u);
  // Must stop before the 3-byte UTF-8 sequence instead of splitting it.
  EXPECT_EQ(ex.text(), std::string(62, 'x'));
}

TEST(FootnoteExtractor, MissingIdReportsNotFound) {
  const char* html = "<html><body><p id=\"other\">Hello</p></body></html>";
  footnote_extract::FootnoteTextExtractor ex("missing");
  EXPECT_TRUE(ex.feed(html, std::strlen(html)));
  EXPECT_FALSE(ex.found());
  EXPECT_TRUE(ex.text().empty());
}

TEST(FootnoteHitTest, MatchesDirectPaddedAndClosestBox) {
  const footnote_hit::HitBox boxes[] = {
      {100, 200, 14, 24, 0},
      {130, 200, 16, 24, 1},
      {300, 400, 20, 24, 2},
  };

  // Direct center hit
  EXPECT_EQ(footnote_hit::hitTest(boxes, 3, 107, 212), 0);
  // Finger-slop hit 10 px outside box 2 (within default 12 px pad)
  EXPECT_EQ(footnote_hit::hitTest(boxes, 3, 290, 392), 2);
  // Miss far outside
  EXPECT_EQ(footnote_hit::hitTest(boxes, 3, 200, 300), -1);
  // Overlap between padded box 0 (center x=107) and box 1 (center x=138): tap at x=125 prefers box 1.
  EXPECT_EQ(footnote_hit::hitTest(boxes, 3, 125, 212), 1);
  EXPECT_EQ(footnote_hit::hitTest(boxes, 3, 118, 212), 0);
}

TEST(FootnoteParser, SkipsLinkedAsideAndRecordsWordSpan) {
  Storage.reset();
  HalFile::readChunk = SIZE_MAX;

  const std::string chapterPath = "/book/OEBPS/text/ch1.xhtml";
  Storage.put(chapterPath,
              "<html><body>"
              "<p>Alpha beta<a href=\"#fn1\">[1]</a> gamma <a href=\"notes.xhtml#fn2\">see note</a> delta.</p>"
              "<aside id=\"fn1\" epub:type=\"footnote\"><p>Hidden popup note text.</p></aside>"
              "<aside id=\"unlinked\" epub:type=\"footnote\"><p>Unlinked note stays visible.</p></aside>"
              "<p>Omega.</p>"
              "</body></html>");

  std::vector<std::unique_ptr<Page>> pages;
  const PageCompleteFn onPage = [](void* ctx, std::unique_ptr<Page> page, uint16_t, uint16_t, uint32_t) {
    static_cast<std::vector<std::unique_ptr<Page>>*>(ctx)->push_back(std::move(page));
  };

  Epub epub;
  GfxRenderer renderer;
  ChapterHtmlSlimParser parser(&epub, chapterPath, renderer, defaultSpec(), onPage, &pages, "/book/OEBPS/text/",
                               "/book/img_", {}, nullptr, nullptr, nullptr);

  ASSERT_TRUE(parser.beginParse());
  ChapterHtmlSlimParser::ParseStatus status;
  while ((status = parser.parseStep()) == ChapterHtmlSlimParser::ParseStatus::More) {
  }
  ASSERT_EQ(status, ChapterHtmlSlimParser::ParseStatus::Done);
  parser.finishParse();

  ASSERT_EQ(pages.size(), 1u);
  std::vector<std::string> words;
  const std::string allText = collectPageWords(*pages[0], &words);

  // Linked <aside id="fn1" epub:type="footnote"> stays out of main flow.
  EXPECT_EQ(allText.find("Hidden"), std::string::npos);
  // Unlinked aside (such as an endnotes chapter) remains readable in flow.
  EXPECT_NE(allText.find("Unlinked"), std::string::npos);
  EXPECT_NE(allText.find("Omega."), std::string::npos);

  ASSERT_EQ(pages[0]->footnotes.size(), 2u);
  const auto& fn1 = pages[0]->footnotes[0];
  EXPECT_STREQ(fn1.number, "1");
  EXPECT_STREQ(fn1.href, "#fn1");
  ASSERT_LT(fn1.startWord, words.size());
  EXPECT_EQ(words[fn1.startWord], "[1]");
  EXPECT_EQ(fn1.wordCount, 1u);

  const auto& fn2 = pages[0]->footnotes[1];
  EXPECT_STREQ(fn2.number, "see note");
  EXPECT_STREQ(fn2.href, "notes.xhtml#fn2");
  ASSERT_LT(static_cast<size_t>(fn2.startWord) + fn2.wordCount, words.size() + 1);
  EXPECT_EQ(words[fn2.startWord], "see");
  EXPECT_EQ(words[fn2.startWord + 1], "note");
  EXPECT_EQ(fn2.wordCount, 2u);

  // Verify Page::serialize / deserialize preserves startWord and wordCount.
  HalFile out;
  ASSERT_TRUE(Storage.openFileForWrite("TST", "/tmp/page.bin", out));
  ASSERT_TRUE(pages[0]->serialize(out));
  out.close();

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TST", "/tmp/page.bin", in));
  auto restored = Page::deserialize(in, "/book/img_");
  ASSERT_NE(restored, nullptr);
  ASSERT_EQ(restored->footnotes.size(), 2u);
  EXPECT_EQ(restored->footnotes[0].startWord, fn1.startWord);
  EXPECT_EQ(restored->footnotes[0].wordCount, fn1.wordCount);
  EXPECT_EQ(restored->footnotes[1].startWord, fn2.startWord);
  EXPECT_EQ(restored->footnotes[1].wordCount, fn2.wordCount);
}
