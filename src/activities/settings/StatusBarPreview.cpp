#include "StatusBarPreview.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>

#include "CrossPointSettings.h"
#include "components/StatusBar.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace statusbar_preview {

void renderPreview(GfxRenderer& renderer, const Rect& bounds, const StatusBarBlock& sb, const bool atChapterStart) {
  if (bounds.width <= 8 || bounds.height <= 8) return;
  renderer.fillRect(bounds.x, bounds.y, bounds.width, bounds.height, false);
  renderer.drawRect(bounds.x, bounds.y, bounds.width, bounds.height, true);

  const Rect inner{bounds.x + 2, bounds.y + 2, bounds.width - 4, bounds.height - 4};
  StatusBarData d;
  d.hasChapters = true;
  d.chapterPage = 12;
  d.chapterPages = 40;
  d.chapterNum = 2;
  d.chapterTotal = 14;
  d.bookPercent = 37;
  d.chapterPercent = 30;
  d.sessionPages = 8;
  d.paragraphPagesLeft = 2;
  d.bookTitle = tr(STR_BOOK_TITLE);
  d.chapterTitle = tr(STR_PREVIEW_HEADING);
  d.author = tr(STR_AUTHOR);
  d.readingFontId = SETTINGS.getReaderFontId();
  d.atChapterStart = atChapterStart;

  // Sample body text line in the middle of the mini-page so spacing and hairline rules read clearly.
  const int sampleFont = UI_10_FONT_ID;
  const int sampleH = renderer.getLineHeight(sampleFont);
  const int sampleY = inner.y + (inner.height - sampleH) / 2;
  const int sampleMaxW = std::max(1, inner.width - 2 * UITheme::getInstance().getMetrics().statusBarHorizontalMargin);
  const std::string sampleLine = renderer.truncatedText(sampleFont, tr(STR_FONT_PREVIEW_TEXT), sampleMaxW);
  const int sampleX = inner.x + (inner.width - renderer.getTextWidth(sampleFont, sampleLine.c_str())) / 2;
  renderer.drawText(sampleFont, sampleX, sampleY, sampleLine.c_str(), true);

  GUI.drawStatusBarV2InRect(renderer, inner, d, sb);
}

}  // namespace statusbar_preview
