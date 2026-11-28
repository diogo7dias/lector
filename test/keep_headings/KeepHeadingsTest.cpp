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
#include "Epub/parsers/PageBreakPolicy.h"

namespace {

struct PageLines {
  std::vector<std::string> lines;
};

void collectPages(void* ctx, std::unique_ptr<Page> page, uint16_t, uint16_t, uint32_t) {
  auto& pages = *static_cast<std::vector<PageLines>*>(ctx);
  PageLines out;
  if (page) {
    for (const auto& element : page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const auto& block = static_cast<PageLine&>(*element).getBlock();
      std::string text;
      for (size_t i = 0; i < block->wordCount(); ++i) {
        if (i > 0) text.push_back(' ');
        text += block->wordText(i);
      }
      out.lines.push_back(std::move(text));
    }
  }
  pages.push_back(std::move(out));
}

// Stub GfxRenderer: each glyph is ~8 px wide, lineHeight = 20 px at lineCompression = 1.0.
// With viewportWidth = 100 and paragraphIndentSpaces = 0, a single word of ~10 chars fits
// per line, making exact multi-line paragraphs deterministic.
std::vector<PageLines> layOut(const std::string& body, const bool keepHeadings, const uint16_t viewportHeight,
                              const uint16_t viewportWidth = 100) {
  const std::string path = "/book/OEBPS/text/ch.xhtml";
  Storage.reset();
  Storage.put(path, "<html><body>" + body + "</body></html>");
  CssParser css("/book/css");
  ReaderRenderSpec spec;
  spec.fontId = 1;
  spec.embeddedStyle = false;
  spec.extraParagraphSpacing = false;
  spec.paragraphIndentSpaces = 0;
  spec.viewportWidth = viewportWidth;
  spec.viewportHeight = viewportHeight;
  spec.keepHeadingsWithText = keepHeadings;
  std::vector<PageLines> pages;
  Epub epub;
  GfxRenderer renderer;
  ChapterHtmlSlimParser parser(&epub, path, renderer, spec, &collectPages, &pages, "/book/OEBPS/text/", "/book/img_",
                               {}, nullptr, nullptr, &css);
  EXPECT_TRUE(parser.beginParse());
  ChapterHtmlSlimParser::ParseStatus status;
  while ((status = parser.parseStep()) == ChapterHtmlSlimParser::ParseStatus::More) {
  }
  EXPECT_EQ(status, ChapterHtmlSlimParser::ParseStatus::Done);
  parser.finishParse();
  return pages;
}

std::string makeLinesParagraph(const std::string& prefix, const int lineCount) {
  // Each token is 9 ASCII chars (~72 px > half of 100 px), forcing one token per line.
  std::string html = "<p>";
  for (int i = 1; i <= lineCount; ++i) {
    if (i > 1) html.push_back(' ');
    html += prefix + "line0" + std::to_string(i);
  }
  html += "</p>";
  return html;
}

TEST(PageBreakPolicy, DisabledCarriesNothing) {
  const auto d = page_break_policy::computeCarry(false, true, false, 1, 1, 3, 2);
  EXPECT_EQ(d.totalLines, 0u);
  EXPECT_EQ(d.paragraphLines, 0u);
}

TEST(PageBreakPolicy, CarriesOrphanAndWidowOnlyOnFourPlusLineParagraphs) {
  // 4-line paragraph orphan (1 on page, 1 overflowing, 2 after).
  EXPECT_EQ(page_break_policy::computeCarry(true, true, false, 1, 1, 2, 0).totalLines, 1u);
  // 4-line paragraph widow (3 on page, 1 overflowing, 0 after).
  EXPECT_EQ(page_break_policy::computeCarry(true, true, false, 3, 3, 0, 0).totalLines, 1u);
  // 4-line paragraph balanced split (2 on page, 2 remaining) -> no carry.
  EXPECT_EQ(page_break_policy::computeCarry(true, true, false, 2, 2, 1, 0).totalLines, 0u);
  // 3-line paragraph orphan -> no carry.
  EXPECT_EQ(page_break_policy::computeCarry(true, true, false, 1, 1, 1, 0).totalLines, 0u);
  // 3-line paragraph widow -> no carry.
  EXPECT_EQ(page_break_policy::computeCarry(true, true, false, 2, 2, 0, 0).totalLines, 0u);
  // Heading alone before overflowing paragraph -> carry heading.
  const auto headingOnly = page_break_policy::computeCarry(true, true, false, 0, 0, 1, 2);
  EXPECT_EQ(headingOnly.totalLines, 2u);
  EXPECT_EQ(headingOnly.paragraphLines, 0u);
  // Heading + 1-line orphan of 4-line paragraph -> carry both heading and orphan.
  const auto headingAndOrphan = page_break_policy::computeCarry(true, true, false, 1, 1, 2, 1);
  EXPECT_EQ(headingAndOrphan.totalLines, 2u);
  EXPECT_EQ(headingAndOrphan.paragraphLines, 1u);
}

TEST(KeepHeadings, HeadingAtBottomMovesToNextPageWhenEnabled) {
  // Stub fontId=1 has lineHeight=12; <h2> (scale 1.5 -> fontId=2) has lineHeight=15.
  // viewportHeight=63 holds 4 body lines (48) + 1 heading line (15); following text overflows.
  const std::string body = makeLinesParagraph("fill", 4) + "<h2>SectionTitle</h2>" + makeLinesParagraph("body", 2);

  const auto pagesOff = layOut(body, false, 63);
  ASSERT_EQ(pagesOff.size(), 2u);
  ASSERT_EQ(pagesOff[0].lines.size(), 5u);
  EXPECT_EQ(pagesOff[0].lines.back(), "SectionTitle");

  const auto pagesOn = layOut(body, true, 63);
  ASSERT_EQ(pagesOn.size(), 2u);
  ASSERT_EQ(pagesOn[0].lines.size(), 4u);
  ASSERT_GE(pagesOn[1].lines.size(), 3u);
  EXPECT_EQ(pagesOn[1].lines.front(), "SectionTitle");
  EXPECT_EQ(pagesOn[1].lines[1], "bodyline01");
}

TEST(KeepHeadings, FourLineOrphanMovesToNextPage) {
  // viewportHeight=60 holds 5 body lines (5 * 12): 4 filler + line 1 of a 4-line paragraph.
  const std::string body = makeLinesParagraph("fill", 4) + makeLinesParagraph("para", 4);

  const auto pagesOff = layOut(body, false, 60);
  ASSERT_EQ(pagesOff.size(), 2u);
  EXPECT_EQ(pagesOff[0].lines.size(), 5u);
  EXPECT_EQ(pagesOff[0].lines.back(), "paraline01");

  const auto pagesOn = layOut(body, true, 60);
  ASSERT_EQ(pagesOn.size(), 2u);
  EXPECT_EQ(pagesOn[0].lines.size(), 4u);
  ASSERT_EQ(pagesOn[1].lines.size(), 4u);
  EXPECT_EQ(pagesOn[1].lines.front(), "paraline01");
}

TEST(KeepHeadings, FourLineWidowPullsPreviousLine) {
  // viewportHeight=60 holds 5 body lines: 2 filler + 3 lines of a 4-line paragraph leaves line 4 alone.
  const std::string body = makeLinesParagraph("fill", 2) + makeLinesParagraph("para", 4);

  const auto pagesOff = layOut(body, false, 60);
  ASSERT_EQ(pagesOff.size(), 2u);
  EXPECT_EQ(pagesOff[0].lines.size(), 5u);
  EXPECT_EQ(pagesOff[1].lines.size(), 1u);
  EXPECT_EQ(pagesOff[1].lines.front(), "paraline04");

  const auto pagesOn = layOut(body, true, 60);
  ASSERT_EQ(pagesOn.size(), 2u);
  EXPECT_EQ(pagesOn[0].lines.size(), 4u);
  ASSERT_EQ(pagesOn[1].lines.size(), 2u);
  EXPECT_EQ(pagesOn[1].lines[0], "paraline03");
  EXPECT_EQ(pagesOn[1].lines[1], "paraline04");
}

TEST(KeepHeadings, ThreeLineParagraphDoesNotCarry) {
  // 3-line paragraph: neither orphan nor widow carry fires.
  const std::string orphanBody = makeLinesParagraph("fill", 4) + makeLinesParagraph("short", 3);
  const auto orphanPages = layOut(orphanBody, true, 60);
  ASSERT_EQ(orphanPages.size(), 2u);
  EXPECT_EQ(orphanPages[0].lines.size(), 5u);
  EXPECT_EQ(orphanPages[0].lines.back(), "shortline01");

  const std::string widowBody = makeLinesParagraph("fill", 3) + makeLinesParagraph("short", 3);
  const auto widowPages = layOut(widowBody, true, 60);
  ASSERT_EQ(widowPages.size(), 2u);
  EXPECT_EQ(widowPages[0].lines.size(), 5u);
  EXPECT_EQ(widowPages[1].lines.size(), 1u);
  EXPECT_EQ(widowPages[1].lines.front(), "shortline03");
}

}  // namespace
