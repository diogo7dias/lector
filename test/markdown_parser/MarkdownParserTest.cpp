#include <GfxRenderer.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "Epub/MarkdownSection.h"
#include "Epub/Page.h"
#include "Epub/parsers/MarkdownParser.h"

namespace {

struct WordRec {
  std::string text;
  EpdFontFamily::Style style;
  int blockFontId;
  int wordFontId;
  int leftInset;
};

struct ParsedDoc {
  std::vector<WordRec> words;
  int borderBoxes = 0;
  int pageCount = 0;
  std::vector<MarkdownTocEntry> toc;
  size_t maxBufferedWords = 0;
  size_t softFlushCount = 0;
  uint32_t yieldCount = 0;
  uint32_t progressCallbacks = 0;
  uint32_t lastProgressBytes = 0;
};

constexpr int BODY_FONT = 1;
constexpr int MONO_FONT = 7;

void collectPage(void* ctx, std::unique_ptr<Page> page, uint16_t, uint16_t, uint32_t) {
  auto& doc = *static_cast<ParsedDoc*>(ctx);
  if (!page) return;
  doc.pageCount++;
  for (const auto& el : page->elements) {
    if (el->getTag() == TAG_PageBorderBox) {
      doc.borderBoxes++;
      continue;
    }
    if (el->getTag() != TAG_PageLine) continue;
    const auto& block = static_cast<PageLine&>(*el).getBlock();
    const int blkFont = block->blockFontId(BODY_FONT);
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      doc.words.push_back({block->wordText(i), block->wordStyle(i), blkFont, block->wordFontId(i, BODY_FONT),
                           block->getBlockStyle().leftInset()});
    }
  }
}

void recordProgress(void* ctx, const uint32_t bytesRead, uint32_t) {
  auto& doc = *static_cast<ParsedDoc*>(ctx);
  doc.progressCallbacks++;
  doc.lastProgressBytes = bytesRead;
}

ParsedDoc parseMd(const std::string& md, const uint16_t viewportHeight = 400, const int monoFontId = 0) {
  const std::string path = "/book/note.md";
  Storage.reset();
  Storage.put(path, md);
  ReaderRenderSpec spec;
  spec.fontId = BODY_FONT;
  spec.viewportWidth = 360;
  spec.viewportHeight = viewportHeight;
  spec.extraParagraphSpacing = false;
  spec.paragraphIndentSpaces = 0;
  GfxRenderer renderer;
  renderer.monospaceFontId = monoFontId;
  ParsedDoc doc;
  MarkdownParser parser(path, renderer, spec, &collectPage, &doc, &recordProgress, &doc);
  EXPECT_TRUE(parser.parseAndBuildPages());
  doc.toc = parser.getToc();
  doc.maxBufferedWords = parser.maxBufferedWords();
  doc.softFlushCount = parser.softFlushCount();
  doc.yieldCount = parser.yieldCount();
  return doc;
}

const WordRec* findWord(const ParsedDoc& doc, const std::string& w) {
  for (const auto& rec : doc.words) {
    if (rec.text == w) return &rec;
  }
  return nullptr;
}

TEST(MarkdownParser, HeadingsProduceTocEntriesAndScaledBoldStyles) {
  const auto doc = parseMd(
      "# Chapter One\n\n"
      "Opening body text.\n\n"
      "## Section Two\n\n"
      "More body text.\n");
  ASSERT_EQ(doc.toc.size(), 2u);
  EXPECT_EQ(doc.toc[0].title, "Chapter One");
  EXPECT_EQ(doc.toc[0].level, 1);
  EXPECT_EQ(doc.toc[0].pageIndex, 0);
  EXPECT_EQ(doc.toc[1].title, "Section Two");
  EXPECT_EQ(doc.toc[1].level, 2);

  const WordRec* ch = findWord(doc, "Chapter");
  const WordRec* sec = findWord(doc, "Section");
  const WordRec* body = findWord(doc, "Opening");
  ASSERT_TRUE(ch && sec && body);
  EXPECT_NE(ch->style & EpdFontFamily::BOLD, 0);
  EXPECT_GT(ch->blockFontId, BODY_FONT);
  EXPECT_NE(sec->style & EpdFontFamily::BOLD, 0);
  EXPECT_EQ(body->style & EpdFontFamily::BOLD, 0);
  EXPECT_EQ(body->blockFontId, BODY_FONT);
}

TEST(MarkdownParser, InlineBoldItalicAndLinksRenderCorrectly) {
  const auto doc =
      parseMd("Normal **boldword** *italicword* ***bothword*** and [LinkLabel](https://example.com/secret).\n");
  const WordRec* normal = findWord(doc, "Normal");
  const WordRec* bold = findWord(doc, "boldword");
  const WordRec* italic = findWord(doc, "italicword");
  const WordRec* both = findWord(doc, "bothword");
  const WordRec* link = findWord(doc, "LinkLabel");
  ASSERT_TRUE(normal && bold && italic && both && link);

  EXPECT_EQ(normal->style & (EpdFontFamily::BOLD | EpdFontFamily::ITALIC), 0);
  EXPECT_NE(bold->style & EpdFontFamily::BOLD, 0);
  EXPECT_EQ(bold->style & EpdFontFamily::ITALIC, 0);
  EXPECT_EQ(italic->style & EpdFontFamily::BOLD, 0);
  EXPECT_NE(italic->style & EpdFontFamily::ITALIC, 0);
  EXPECT_NE(both->style & EpdFontFamily::BOLD, 0);
  EXPECT_NE(both->style & EpdFontFamily::ITALIC, 0);
  EXPECT_EQ(findWord(doc, "https://example.com/secret"), nullptr);
}

TEST(MarkdownParser, BlockquotesIndentAndDrawLeftBar) {
  const auto doc = parseMd("Plain\n\n> Quoted line one\n> quoted line two\n");
  const WordRec* plain = findWord(doc, "Plain");
  const WordRec* quoted = findWord(doc, "Quoted");
  ASSERT_TRUE(plain && quoted);
  EXPECT_EQ(plain->leftInset, 0);
  EXPECT_GT(quoted->leftInset, 0);
  EXPECT_GE(doc.borderBoxes, 1);
}

TEST(MarkdownParser, UnorderedAndOrderedListsIndentWithMarkers) {
  const auto doc = parseMd("- BulletItem\n* StarItem\n\n1. FirstItem\n2. SecondItem\n");
  const WordRec* bullet = findWord(doc, "\xe2\x80\xa2");
  const WordRec* bulletItem = findWord(doc, "BulletItem");
  const WordRec* one = findWord(doc, "1.");
  const WordRec* firstItem = findWord(doc, "FirstItem");
  ASSERT_TRUE(bullet && bulletItem && one && firstItem);
  EXPECT_GT(bulletItem->leftInset, 0);
  EXPECT_GT(firstItem->leftInset, 0);
}

// A paragraph is one line in most Markdown editors; a long one must not lose its tail,
// and must soft-flush at 750 words to keep C3 heap bounded.
TEST(MarkdownParser, ALongSingleLineParagraphKeepsEveryWord) {
  std::string md;
  for (int i = 0; i < 1200; ++i) md += "w" + std::to_string(i) + " ";
  md += "\n";
  ASSERT_GT(md.size(), 4096u);
  const auto doc = parseMd(md);
  EXPECT_EQ(doc.words.size(), 1200u);
  EXPECT_NE(findWord(doc, "w0"), nullptr);
  EXPECT_NE(findWord(doc, "w1199"), nullptr);
  EXPECT_GE(doc.softFlushCount, 1u);
  EXPECT_LE(doc.maxBufferedWords, MarkdownParser::SOFT_FLUSH_WORDS);
}

TEST(MarkdownParser, CodeFencesUseMonospaceAndIgnoreHashesAndInlineMarkers) {
  const std::string md =
      "# RealHeading\n\n"
      "```python\n"
      "# NotAHeading\n"
      "x = **literal** [raw](https://example.com)\n"
      "```\n\n"
      "After fence `inlinecode` end.\n";
  const auto doc = parseMd(md, 400, MONO_FONT);

  ASSERT_EQ(doc.toc.size(), 1u);
  EXPECT_EQ(doc.toc[0].title, "RealHeading");

  const WordRec* commentHash = findWord(doc, "#");
  const WordRec* notHeading = findWord(doc, "NotAHeading");
  const WordRec* literalStars = findWord(doc, "**literal**");
  const WordRec* inlineCode = findWord(doc, "inlinecode");
  const WordRec* after = findWord(doc, "After");
  ASSERT_TRUE(commentHash && notHeading && literalStars && inlineCode && after);

  EXPECT_EQ(notHeading->blockFontId, MONO_FONT);
  EXPECT_EQ(notHeading->style & EpdFontFamily::BOLD, 0);
  EXPECT_EQ(literalStars->blockFontId, MONO_FONT);
  EXPECT_EQ(inlineCode->wordFontId, MONO_FONT);
  EXPECT_EQ(after->wordFontId, BODY_FONT);

  // Without an installed monospace font, code fences fall back cleanly to the body font.
  const auto fallback = parseMd(md, 400, 0);
  const WordRec* fbCode = findWord(fallback, "NotAHeading");
  ASSERT_NE(fbCode, nullptr);
  EXPECT_EQ(fbCode->blockFontId, BODY_FONT);
  ASSERT_EQ(fallback.toc.size(), 1u);
}

TEST(MarkdownParser, YieldsAndReportsProgressOnLargeDocuments) {
  std::string md;
  for (int i = 0; i < 150; ++i) {
    md += "Line number " + std::to_string(i) + " with enough text to wrap.\n\n";
  }
  const auto doc = parseMd(md, 200);
  EXPECT_GE(doc.yieldCount, 2u);
  EXPECT_GE(doc.progressCallbacks, 2u);
  EXPECT_EQ(doc.lastProgressBytes, static_cast<uint32_t>(md.size()));
}

TEST(MarkdownSection, BuildsSerializesAndReloadsSectionBinCache) {
  const std::string md = "# Title\n\nHello **world** from [Lector](https://lector.example).\n\n> A quote.\n";
  const std::string path = "/book/readme.md";
  Storage.reset();
  Storage.put(path, md);
  ReaderRenderSpec spec;
  spec.fontId = BODY_FONT;
  spec.viewportWidth = 360;
  spec.viewportHeight = 400;
  GfxRenderer renderer;

  MarkdownSection writer("/cache/md");
  ASSERT_TRUE(writer.build(path, renderer, spec, static_cast<uint32_t>(md.size())));
  EXPECT_GE(writer.pageCount(), 1);
  ASSERT_EQ(writer.toc().size(), 1u);
  EXPECT_EQ(writer.toc()[0].title, "Title");

  MarkdownSection reader("/cache/md");
  ASSERT_TRUE(reader.load(spec, static_cast<uint32_t>(md.size())));
  EXPECT_EQ(reader.pageCount(), writer.pageCount());
  ASSERT_EQ(reader.toc().size(), 1u);
  EXPECT_EQ(reader.toc()[0].title, "Title");

  auto page = reader.loadPage(0);
  ASSERT_NE(page, nullptr);
  EXPECT_FALSE(page->elements.empty());

  ReaderRenderSpec other = spec;
  other.keepHeadingsWithText = !spec.keepHeadingsWithText;
  MarkdownSection mismatched("/cache/md");
  EXPECT_FALSE(mismatched.load(other, static_cast<uint32_t>(md.size())));
}

}  // namespace
