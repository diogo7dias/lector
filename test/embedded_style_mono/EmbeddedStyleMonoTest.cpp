#include <Epub.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "Epub/Page.h"
#include "Epub/css/CssParser.h"
#include "Epub/css/CssStyle.h"
#include "Epub/parsers/ChapterHtmlSlimParser.h"

namespace {

struct WordInfo {
  std::string text;
  int fontId;
  int blockFontId;
  int leftInset;
  EpdFontFamily::Style style;
};

struct LayoutOut {
  std::vector<WordInfo> words;
  int borderBoxCount = 0;
};

constexpr int SECTION_FONT_ID = 1;
constexpr int MONO_FONT_ID = 7;

void collect(void* ctx, std::unique_ptr<Page> page, uint16_t, uint16_t, uint32_t) {
  auto& out = *static_cast<LayoutOut*>(ctx);
  if (!page) return;
  for (const auto& element : page->elements) {
    if (element->getTag() == TAG_PageBorderBox) {
      out.borderBoxCount++;
      continue;
    }
    if (element->getTag() != TAG_PageLine) continue;
    const auto& block = static_cast<PageLine&>(*element).getBlock();
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      out.words.push_back({block->wordText(i), block->wordFontId(i, SECTION_FONT_ID),
                           block->blockFontId(SECTION_FONT_ID), block->getBlockStyle().leftInset(),
                           block->wordStyle(i)});
    }
  }
}

LayoutOut layOut(const std::string& body, const std::string& stylesheet, const uint8_t embeddedStyle,
                 const int monoFontId) {
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
  spec.fontId = SECTION_FONT_ID;
  spec.embeddedStyle = embeddedStyle;
  spec.viewportWidth = 456;
  spec.viewportHeight = 760;
  LayoutOut out;
  Epub epub;
  GfxRenderer renderer;
  renderer.monospaceFontId = monoFontId;
  ChapterHtmlSlimParser parser(&epub, path, renderer, spec, &collect, &out, "/book/OEBPS/text/", "/book/img_", {},
                               nullptr, nullptr, &css);
  EXPECT_TRUE(parser.beginParse());
  ChapterHtmlSlimParser::ParseStatus status;
  while ((status = parser.parseStep()) == ChapterHtmlSlimParser::ParseStatus::More) {
  }
  EXPECT_EQ(status, ChapterHtmlSlimParser::ParseStatus::Done);
  parser.finishParse();
  return out;
}

const WordInfo* findWord(const LayoutOut& out, const std::string& text) {
  for (const auto& w : out.words) {
    if (w.text == text) return &w;
  }
  return nullptr;
}

TEST(CssStyleKeepBuckets, TextOnlyKeepsTextAndDropsLayoutAndSizes) {
  CssStyle full;
  full.fontWeight = CssFontWeight::Bold;
  full.defined.fontWeight = 1;
  full.fontStyle = CssFontStyle::Italic;
  full.defined.fontStyle = 1;
  full.textDecoration = CssTextDecoration::Underline;
  full.defined.textDecoration = 1;
  full.smallCaps = true;
  full.defined.smallCaps = 1;
  full.verticalAlign = CssVerticalAlign::Super;
  full.defined.verticalAlign = 1;
  full.preserveWhitespace = true;
  full.defined.whiteSpace = 1;
  full.display = CssDisplay::None;
  full.defined.display = 1;
  full.direction = CssTextDirection::Rtl;
  full.defined.direction = 1;

  full.marginLeft = {16.0f, CssUnit::Pixels};
  full.defined.marginLeft = 1;
  full.paddingLeft = {12.0f, CssUnit::Pixels};
  full.defined.paddingLeft = 1;
  full.fontSize = {1.5f, CssUnit::Em};
  full.defined.fontSize = 1;
  full.borderLeft = {2, CssBorderStyle::Solid};
  full.defined.borderLeft = 1;
  full.shaded = true;
  full.defined.shaded = 1;
  full.textAlign = CssTextAlign::Center;
  full.defined.textAlign = 1;
  full.textIndent = {20.0f, CssUnit::Pixels};
  full.defined.textIndent = 1;

  CssStyle textOnly = full;
  textOnly.keepBuckets(EMBEDDED_STYLE_TEXT_ONLY);
  EXPECT_TRUE(textOnly.hasFontWeight());
  EXPECT_TRUE(textOnly.hasFontStyle());
  EXPECT_TRUE(textOnly.hasTextDecoration());
  EXPECT_TRUE(textOnly.hasSmallCaps());
  EXPECT_TRUE(textOnly.hasVerticalAlign());
  EXPECT_TRUE(textOnly.defined.whiteSpace);
  EXPECT_TRUE(textOnly.hasDisplay());
  EXPECT_TRUE(textOnly.hasDirection());

  EXPECT_FALSE(textOnly.hasMarginLeft());
  EXPECT_EQ(textOnly.marginLeft.value, 0.0f);
  EXPECT_FALSE(textOnly.hasPaddingLeft());
  EXPECT_EQ(textOnly.paddingLeft.value, 0.0f);
  EXPECT_FALSE(textOnly.hasFontSize());
  EXPECT_FALSE(textOnly.hasVisibleBorder());
  EXPECT_FALSE(textOnly.shaded);
  EXPECT_FALSE(textOnly.hasTextAlign());
  EXPECT_FALSE(textOnly.hasTextIndent());

  CssStyle off = full;
  off.keepBuckets(EMBEDDED_STYLE_OFF);
  EXPECT_FALSE(off.hasFontWeight());
  EXPECT_FALSE(off.hasFontStyle());
  EXPECT_FALSE(off.hasTextDecoration());
  EXPECT_FALSE(off.hasSmallCaps());
  EXPECT_FALSE(off.hasVerticalAlign());
  EXPECT_FALSE(off.defined.whiteSpace);
  EXPECT_FALSE(off.hasDisplay());
  EXPECT_FALSE(off.hasDirection());
}

TEST(MonospaceCodeTags, UsesInstalledMonospaceFontForCodePreTtKbd) {
  const std::string html =
      "<p>plain <code>inlinecode</code> <tt>teletype</tt> <kbd>keycap</kbd></p>"
      "<pre>preformatted</pre>";
  const auto out = layOut(html, "", EMBEDDED_STYLE_FULL, MONO_FONT_ID);

  const WordInfo* plain = findWord(out, "plain");
  const WordInfo* code = findWord(out, "inlinecode");
  const WordInfo* tt = findWord(out, "teletype");
  const WordInfo* kbd = findWord(out, "keycap");
  const WordInfo* pre = findWord(out, "preformatted");
  ASSERT_TRUE(plain && code && tt && kbd && pre);

  EXPECT_EQ(plain->fontId, SECTION_FONT_ID);
  EXPECT_EQ(code->fontId, MONO_FONT_ID);
  EXPECT_EQ(tt->fontId, MONO_FONT_ID);
  EXPECT_EQ(kbd->fontId, MONO_FONT_ID);
  EXPECT_EQ(pre->fontId, MONO_FONT_ID);
}

TEST(MonospaceCodeTags, FallsBackCleanlyWhenMonospaceFontMissing) {
  const std::string html = "<p>plain <code>inlinecode</code> <tt>teletype</tt> <kbd>keycap</kbd></p><pre>preword</pre>";
  const auto out = layOut(html, "", EMBEDDED_STYLE_FULL, 0);

  for (const char* token : {"plain", "inlinecode", "teletype", "keycap", "preword"}) {
    const WordInfo* w = findWord(out, token);
    ASSERT_NE(w, nullptr) << token;
    EXPECT_EQ(w->fontId, SECTION_FONT_ID) << token;
  }
}

TEST(EmbeddedStyleMode, TextOnlyKeepsWeightAndHidesDisplayNoneWhileDroppingBoxesAndSizes) {
  const std::string css =
      ".styled { font-weight: bold; font-style: italic; font-size: 1.5em; margin-left: 30px; "
      "padding-left: 10px; border-left: 2px solid black; }"
      ".gone { display: none; }";
  const std::string html = "<p class=\"styled\">sample</p><p class=\"gone\">hidden</p>";

  const auto full = layOut(html, css, EMBEDDED_STYLE_FULL, 0);
  const WordInfo* fullWord = findWord(full, "sample");
  ASSERT_NE(fullWord, nullptr);
  EXPECT_EQ(findWord(full, "hidden"), nullptr);
  EXPECT_NE(fullWord->style & EpdFontFamily::BOLD, 0);
  EXPECT_NE(fullWord->style & EpdFontFamily::ITALIC, 0);
  EXPECT_GT(fullWord->leftInset, 0);
  EXPECT_GT(fullWord->blockFontId, SECTION_FONT_ID);
  EXPECT_GT(full.borderBoxCount, 0);

  const auto textOnly = layOut(html, css, EMBEDDED_STYLE_TEXT_ONLY, 0);
  const WordInfo* textWord = findWord(textOnly, "sample");
  ASSERT_NE(textWord, nullptr);
  EXPECT_EQ(findWord(textOnly, "hidden"), nullptr);
  EXPECT_NE(textWord->style & EpdFontFamily::BOLD, 0);
  EXPECT_NE(textWord->style & EpdFontFamily::ITALIC, 0);
  EXPECT_EQ(textWord->leftInset, 0);
  EXPECT_EQ(textWord->blockFontId, SECTION_FONT_ID);
  EXPECT_EQ(textOnly.borderBoxCount, 0);

  const auto off = layOut(html, css, EMBEDDED_STYLE_OFF, 0);
  const WordInfo* offWord = findWord(off, "sample");
  ASSERT_NE(offWord, nullptr);
  EXPECT_NE(findWord(off, "hidden"), nullptr);
  EXPECT_EQ(offWord->style & EpdFontFamily::BOLD, 0);
  EXPECT_EQ(offWord->leftInset, 0);
  EXPECT_EQ(off.borderBoxCount, 0);
}

}  // namespace
