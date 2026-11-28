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

struct PlacedLine {
  std::string firstWord;
  int16_t x;
  int16_t y;
  int fontId;
};

struct PlacedRule {
  uint16_t width;
  uint8_t thickness;
  int16_t y;
};

struct TableOut {
  std::vector<PlacedLine> lines;
  std::vector<PlacedRule> rules;
};

constexpr int BODY_FONT_ID = 3;  // >1 so stub resolveFontVariant steps down to 2

void collect(void* ctx, std::unique_ptr<Page> page, uint16_t, uint16_t, uint32_t) {
  auto& out = *static_cast<TableOut*>(ctx);
  if (!page) return;
  for (const auto& element : page->elements) {
    if (element->getTag() == TAG_PageLine) {
      const auto& pl = static_cast<PageLine&>(*element);
      const auto& block = pl.getBlock();
      out.lines.push_back(
          {block->wordCount() ? block->wordText(0) : "", pl.xPos, pl.yPos, block->blockFontId(BODY_FONT_ID)});
    } else if (element->getTag() == TAG_PageHorizontalRule) {
      const auto& hr = static_cast<PageHorizontalRule&>(*element);
      out.rules.push_back({hr.getWidth(), hr.getThickness(), hr.yPos});
    }
  }
}

TableOut layOut(const std::string& body, const uint16_t viewportWidth = 400) {
  const std::string path = "/book/OEBPS/text/ch.xhtml";
  Storage.reset();
  Storage.put(path, "<html><body>" + body + "</body></html>");
  CssParser css("/book/css");
  ReaderRenderSpec spec;
  spec.fontId = BODY_FONT_ID;
  spec.embeddedStyle = false;
  spec.extraParagraphSpacing = false;
  spec.paragraphIndentSpaces = 0;
  spec.viewportWidth = viewportWidth;
  spec.viewportHeight = 760;
  TableOut out;
  Epub epub;
  GfxRenderer renderer;
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

const PlacedLine* findLine(const TableOut& out, const std::string& word) {
  for (const auto& l : out.lines) {
    if (l.firstWord == word) return &l;
  }
  return nullptr;
}

TEST(BetterTables, WideTableStepsCellTextDownOneSize) {
  // Narrow table fits easily in 400px -> keeps BODY_FONT_ID.
  const auto narrow = layOut("<table><tr><td>A</td><td>B</td></tr></table>", 400);
  const PlacedLine* a = findLine(narrow, "A");
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->fontId, BODY_FONT_ID);

  // Wide table (prefTotal > 200px) -> steps down one size (BODY_FONT_ID - 1).
  const auto wide = layOut(
      "<table><tr>"
      "<td>AlphaBetaGammaDelta</td>"
      "<td>EpsilonZetaEtaTheta</td>"
      "<td>IotaKappaLambdaMu</td>"
      "</tr></table>",
      200);
  const PlacedLine* first = findLine(wide, "AlphaBetaGammaDelta");
  ASSERT_NE(first, nullptr);
  EXPECT_LT(first->fontId, BODY_FONT_ID);
}

TEST(BetterTables, ColspanSumsSpannedColumnsInsteadOfStacking) {
  const auto out = layOut(
      "<table>"
      "<tr><td colspan=\"2\">Spanned</td><td>RightCell</td></tr>"
      "<tr><td>Col0</td><td>Col1</td><td>Col2</td></tr>"
      "</table>",
      360);
  const PlacedLine* spanned = findLine(out, "Spanned");
  const PlacedLine* rightCell = findLine(out, "RightCell");
  const PlacedLine* col0 = findLine(out, "Col0");
  const PlacedLine* col2 = findLine(out, "Col2");
  ASSERT_TRUE(spanned && rightCell && col0 && col2);

  // Row 1 stayed a single grid row (Spanned and RightCell share yPos, not stacked).
  EXPECT_EQ(spanned->y, rightCell->y);
  // RightCell aligns with Col2 (its x equals Col0 + Col1 combined width).
  EXPECT_EQ(rightCell->x, col2->x);
  EXPECT_GT(rightCell->x, col0->x);
}

TEST(BetterTables, HeaderRowDrawsTwoPixelRule) {
  const auto out = layOut(
      "<table>"
      "<tr><th>Name</th><th>Value</th></tr>"
      "<tr><td>Alpha</td><td>One</td></tr>"
      "</table>",
      360);
  ASSERT_GE(out.rules.size(), 2u);
  EXPECT_EQ(out.rules[0].thickness, 2);
  EXPECT_EQ(out.rules[0].width, 360);
  EXPECT_EQ(out.rules[1].thickness, 1);
}

// A <th> that leads a row of <td> is a row header, not a header row: no heavy rule under it.
TEST(BetterTables, RowHeaderCellDoesNotMakeAHeaderRow) {
  const auto out = layOut(
      "<table>"
      "<tr><th>Name</th><th>Value</th></tr>"
      "<tr><th>Alpha</th><td>One</td></tr>"
      "<tr><th>Beta</th><td>Two</td></tr>"
      "</table>",
      360);
  ASSERT_EQ(out.rules.size(), 3u);
  EXPECT_EQ(out.rules[0].thickness, 2);
  EXPECT_EQ(out.rules[1].thickness, 1);
  EXPECT_EQ(out.rules[2].thickness, 1);
}

TEST(BetterTables, SixColumnGridOnlyOnPsramBoards) {
  const auto out = layOut(
      "<table><tr>"
      "<td>C1</td><td>C2</td><td>C3</td><td>C4</td><td>C5</td><td>C6</td>"
      "</tr></table>",
      480);
  const PlacedLine* c1 = findLine(out, "C1");
  const PlacedLine* c6 = findLine(out, "C6");
  ASSERT_TRUE(c1 && c6);
#if defined(BOARD_HAS_PSRAM)
  // X4 Pro (PSRAM): 6 columns stay on one horizontal grid row.
  EXPECT_EQ(c1->y, c6->y);
  EXPECT_GT(c6->x, c1->x);
#else
  // C3 (no PSRAM): 6 columns exceed the 4-column cap and stack vertically.
  EXPECT_GT(c6->y, c1->y);
#endif
}

}  // namespace
