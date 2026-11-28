// An unstyled <blockquote> is inset from both sides (CrossInk 43b3a78aa); a side the book's
// CSS sets keeps the book's value.
#include <Epub.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "Epub/Page.h"
#include "Epub/css/CssParser.h"
#include "Epub/parsers/ChapterHtmlSlimParser.h"

namespace {

struct Line {
  std::string firstWord;
  int left, right;
};

void collect(void* ctx, std::unique_ptr<Page> page, uint16_t, uint16_t, uint32_t) {
  auto& lines = *static_cast<std::vector<Line>*>(ctx);
  if (!page) return;
  for (const auto& element : page->elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto& block = static_cast<PageLine&>(*element).getBlock();
    lines.push_back({block->wordCount() ? block->wordText(0) : "", block->getBlockStyle().leftInset(),
                     block->getBlockStyle().rightInset()});
  }
}

std::vector<Line> layOut(const std::string& body, const std::string& stylesheet = "", bool bookStyle = true) {
  const std::string path = "/book/OEBPS/text/ch.xhtml";
  Storage.reset();
  Storage.put(path, "<html><body>" + body + "</body></html>");
  CssParser css("/book/css");
  if (!stylesheet.empty()) {
    Storage.put("/book/style.css", stylesheet);
    HalFile file;
    if (Storage.openFileForRead("T", "/book/style.css", file)) css.loadFromStream(file);
  }
  ReaderRenderSpec spec;
  spec.fontId = 1;
  spec.embeddedStyle = bookStyle ? EMBEDDED_STYLE_FULL : EMBEDDED_STYLE_OFF;
  spec.viewportWidth = 456;
  spec.viewportHeight = 760;
  std::vector<Line> lines;
  Epub epub;
  GfxRenderer renderer;
  ChapterHtmlSlimParser parser(&epub, path, renderer, spec, &collect, &lines, "/book/OEBPS/text/", "/book/img_", {},
                               nullptr, nullptr, &css);
  EXPECT_TRUE(parser.beginParse());
  ChapterHtmlSlimParser::ParseStatus status;
  while ((status = parser.parseStep()) == ChapterHtmlSlimParser::ParseStatus::More) {
  }
  EXPECT_EQ(status, ChapterHtmlSlimParser::ParseStatus::Done);
  parser.finishParse();
  return lines;
}

const Line* find(const std::vector<Line>& lines, const std::string& word) {
  for (const auto& line : lines)
    if (line.firstWord == word) return &line;
  return nullptr;
}

TEST(BlockquoteInset, UnstyledQuoteIsInsetBothSides) {
  for (const bool bookStyle : {true, false}) {
    const auto lines = layOut("<p>plain</p><blockquote><p>quoted</p></blockquote>", "", bookStyle);
    const Line* plain = find(lines, "plain");
    const Line* quoted = find(lines, "quoted");
    ASSERT_TRUE(plain && quoted);
    EXPECT_EQ(plain->left, 0);
    EXPECT_GT(quoted->left, 0);
    EXPECT_EQ(quoted->left, quoted->right);
  }
}

TEST(BlockquoteInset, NestedQuotesKeepHalfTheColumn) {
  const auto lines = layOut(
      "<blockquote><blockquote><blockquote><blockquote><blockquote><blockquote><p>deep</p>"
      "</blockquote></blockquote></blockquote></blockquote></blockquote></blockquote>");
  const Line* deep = find(lines, "deep");
  ASSERT_TRUE(deep);
  EXPECT_GT(deep->left, 0);
  EXPECT_LE(deep->left + deep->right, 456 / 2);
}

TEST(BlockquoteInset, BookMarginWinsOnItsSide) {
  const auto lines = layOut("<blockquote><p>quoted</p></blockquote>", "blockquote { margin-left: 0; }");
  const Line* quoted = find(lines, "quoted");
  ASSERT_TRUE(quoted);
  EXPECT_EQ(quoted->left, 0);
  EXPECT_GT(quoted->right, 0);
}

}  // namespace
