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
  int leftInset;
};

struct ParsedDoc {
  std::vector<WordRec> words;
  int borderBoxes = 0;
  int pageCount = 0;
  std::vector<MarkdownTocEntry> toc;
};

constexpr int BODY_FONT = 1;

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
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      doc.words.push_back(
          {block->wordText(i), block->wordStyle(i), block->blockFontId(BODY_FONT), block->getBlockStyle().leftInset()});
    }
  }
}

ParsedDoc parseMd(const std::string& md, const uint16_t viewportHeight = 400) {
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
  ParsedDoc doc;
  MarkdownParser parser(path, renderer, spec, &collectPage, &doc);
  EXPECT_TRUE(parser.parseAndBuildPages());
  doc.toc = parser.getToc();
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
  // URL must never appear in rendered words.
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

  // Changing a spec field invalidates the cache.
  ReaderRenderSpec other = spec;
  other.viewportWidth = 300;
  MarkdownSection mismatched("/cache/md");
  EXPECT_FALSE(mismatched.load(other, static_cast<uint32_t>(md.size())));
}

}  // namespace
