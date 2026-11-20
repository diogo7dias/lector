#include "RuledLines.h"

#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <GfxRenderer.h>

#include "CrossPointSettings.h"

namespace ruled_lines {

// Ruled Lines under each text row, at the row's bottom edge as the layout placed it: the
// height below repeats ChapterHtmlSlimParser::addLineToPage, so headings, CSS-scaled text,
// ruby and taller inline words get their own rule.
void drawPage(const GfxRenderer& renderer, const Page& page, const int fontId, const float compression,
              const int marginLeft, const int marginTop, const int marginRight, const int marginBottom) {
  const int step = dotStep(SETTINGS.ruledLines);
  if (step == 0) return;
  const int width = renderer.getScreenWidth() - marginLeft - marginRight;
  const int bottom = renderer.getScreenHeight() - marginBottom;  // keeps the status bar clear
  const auto isBlack = [&renderer](int x, int y) { return renderer.isPixelBlack(x, y); };
  const auto plot = [&renderer](int x, int y) { renderer.drawPixel(x, y); };
  for (const auto& element : page.elements) {
    if (!element || element->getTag() != TAG_PageLine) continue;
    const auto& line = static_cast<const PageLine&>(*element);
    const auto& block = line.getBlock();
    if (!block) continue;
    const int lineFontId = block->blockFontId(fontId);
    const int textHeight = renderer.getLineHeight(lineFontId, compression) + block->extraAscent(renderer, fontId) +
                           block->getRubyShift(renderer.getFontAscenderSize(lineFontId));
    const int y = marginTop + line.yPos + textHeight - 1;
    if (y < marginTop || y >= bottom) continue;
    drawRow(marginLeft, width, y, step, isBlack, plot);
  }
}

}  // namespace ruled_lines
