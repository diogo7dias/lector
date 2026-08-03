#include "TextSettingsPreview.h"

#include <EpdFontFamily.h>
#include <Epub/ParsedText.h>
#include <Epub/blocks/BlockStyle.h>
#include <Epub/blocks/TextBlock.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>

#include "CrossPointSettings.h"
#include "fontIds.h"

namespace textsettings {

namespace {

// Map the paragraph-alignment setting to the engine's CssTextAlign (BOOK_STYLE = justified)
CssTextAlign toCssAlign(uint8_t align) {
  if (align == CrossPointSettings::BOOK_STYLE) return CssTextAlign::Justify;
  return static_cast<CssTextAlign>(align);
}

// The middle dot the guide-dot aid draws between words (U+00B7, UTF-8).
constexpr char GUIDE_DOT_UTF8[] = "\xc2\xb7";

// Horizontal reading margin, resolved exactly like EpubReaderActivity::computeReaderMargins
// does for the page: Dynamic Margins replaces the fixed margin with a width aimed at ~62
// characters per line. Measured against the FULL screen, not the preview pane, because the
// screen is the viewport the reader will lay the real page out in — so the pane shows the
// same margin the page will get.
int resolveHorizontalMargin(const GfxRenderer& renderer, int fontId) {
  if (!SETTINGS.dynamicMargins) return SETTINGS.screenMargin;

  int viewableTop, viewableRight, viewableBottom, viewableLeft;
  renderer.getOrientedViewableTRBL(&viewableTop, &viewableRight, &viewableBottom, &viewableLeft);
  const int sampleWidth = renderer.getTextWidth(fontId, "abcdefghijklmnopqrstuvwxyz");
  const int avgCharWidth = (sampleWidth > 0) ? sampleWidth / 26 : 8;
  const int targetTextWidth = 62 * avgCharWidth;
  const int availableWidth = renderer.getScreenWidth() - viewableLeft - viewableRight;
  const int minDynamicMargin = (SETTINGS.dynamicMargins >= 2) ? 20 : 10;
  return std::max(minDynamicMargin, std::min(55, (availableWidth - targetTextWidth) / 2));
}

// Lay the sample text out through the reader engine into layout.lines
void relayout(PreviewLayout& layout, const GfxRenderer& renderer, int fontId, int textWidth) {
  layout.lines.clear();

  BlockStyle style;
  style.alignment = toCssAlign(SETTINGS.paragraphAlignment);
  style.textAlignDefined = true;  // honor the user's choice; RTL auto-detected from text

  ParsedText parsed(SETTINGS.extraParagraphSpacing != 0, SETTINGS.hyphenationEnabled != 0,
                    SETTINGS.focusReadingEnabled != 0, SETTINGS.guideDotsEnabled != 0, style,
                    SETTINGS.firstLineIndentMode, SETTINGS.firstLineIndentPercent);

  // Feed one space-separated word at a time; addWord handles NFC/CJK/RTL/focus splitting
  const char* text = I18N.get(StrId::STR_FONT_PREVIEW_TEXT);
  std::string word;
  for (const char* p = text;; p++) {
    if (*p == ' ' || *p == '\0') {
      if (!word.empty()) {
        parsed.addWord(word, EpdFontFamily::REGULAR);
        word.clear();
      }
      if (*p == '\0') break;
    } else {
      word.push_back(*p);
    }
  }

  parsed.layoutAndExtractLines(
      renderer, fontId, static_cast<uint16_t>(textWidth),
      [&layout](std::shared_ptr<TextBlock> line, uint32_t) { layout.lines.push_back(std::move(line)); });
}

}  // namespace

void renderPreview(const GfxRenderer& renderer, PreviewLayout& layout, int previewPadding, int labelGap, int top,
                   int height, const char* familyName, const char* sizeName) {
  const int left = previewPadding;
  const int width = renderer.getScreenWidth() - (previewPadding * 2);
  if (width <= 0 || height <= 0) return;

  const int labelH = renderer.getTextHeight(UI_10_FONT_ID);
  const int labelReserved = labelH + labelGap + previewPadding;

  char labelBuf[128];
  snprintf(labelBuf, sizeof(labelBuf), "%s \"%s, %s\"", tr(STR_PREVIEW), familyName, sizeName);
  const int labelY = top + height - previewPadding - labelH;
  renderer.drawText(UI_10_FONT_ID, left, labelY, labelBuf);

  const int fontId = SETTINGS.getReaderFontId();
  if (fontId == 0) return;

  const int lineH = renderer.getTextHeight(fontId);
  if (lineH <= 0) return;

  const int marginH = resolveHorizontalMargin(renderer, fontId);
  const int textLeft = left + marginH;
  const int textWidth = width - 2 * marginH;
  if (textWidth <= 0) return;

  const float compression = SETTINGS.getReaderLineCompression();
  const int lineAdvance = std::max(1, renderer.getLineHeight(fontId, compression));
  // Same stack the parser applies after each paragraph: the Extra Spacing toggle adds half
  // a line, the Paragraph Spacing percentage adds its share on top of it.
  // See ChapterHtmlSlimParser.cpp finishParagraph.
  const int paragraphGap =
      (SETTINGS.extraParagraphSpacing ? lineAdvance / 2 : 0) + lineAdvance * SETTINGS.paragraphSpacing / 100;

  // Re-lay-out (and re-prewarm glyphs) only when a layout-affecting setting or the
  // geometry changed; else reuse the cache. The prewarm inputs are (fontId, constant
  // sample text, styleMask<-focusReading), all of which are key fields, so a matching
  // key means an identical prewarm call. This relies on nothing else evicting the SD
  // glyph cache while this activity is up — true today: the only evictor is
  // FontCacheManager::PrewarmScope, used solely by the reader/dictionary activities.
  const PreviewKey key{.fontId = fontId,
                       .fontPointSize = SETTINGS.fontPointSize,
                       .screenMargin = marginH,
                       .textWidth = textWidth,
                       .lineCompression = compression,
                       .alignment = SETTINGS.paragraphAlignment,
                       .extraParagraphSpacing = SETTINGS.extraParagraphSpacing != 0,
                       .focusReading = SETTINGS.focusReadingEnabled != 0,
                       .hyphenation = SETTINGS.hyphenationEnabled != 0,
                       .guideDots = SETTINGS.guideDotsEnabled != 0,
                       .firstLineIndentMode = SETTINGS.firstLineIndentMode,
                       .firstLineIndentPercent = SETTINGS.firstLineIndentPercent};
  if (key != layout.key) {
    if (auto* fcm = renderer.getFontCacheManager()) {
      // The guide dot is not in the sample sentence, so it has to be prewarmed
      // explicitly or an SD font would miss the glyph on the first draw.
      std::string prewarmText = I18N.get(StrId::STR_FONT_PREVIEW_TEXT);
      if (SETTINGS.guideDotsEnabled) prewarmText += GUIDE_DOT_UTF8;
      fcm->prewarmCache(fontId, prewarmText.c_str(), SETTINGS.focusReadingEnabled ? 0x03 : 0x01);
    }
    relayout(layout, renderer, fontId, textWidth);
    layout.key = key;
  }

  // Vertical reading margins, from the same source the page uses (uniform margins put
  // screenMargin on every side). The pane is only a slice of the page height, so a
  // full-size margin would push the sample out of view entirely: each side is capped at
  // a third of the pane's text area, which still shows the setting moving.
  const int paneTextHeight = height - labelReserved - previewPadding;
  if (paneTextHeight <= 0) return;
  const int verticalCap = paneTextHeight / 3;
  const int topMargin = SETTINGS.uniformMargins ? SETTINGS.screenMargin : SETTINGS.screenMarginTop;
  const int bottomMargin = SETTINGS.uniformMargins ? SETTINGS.screenMargin : SETTINGS.screenMarginBottom;
  const int insetTop = std::min(topMargin, verticalCap);
  const int insetBottom = std::min(bottomMargin, verticalCap);

  int y = top + previewPadding + insetTop;
  const int textBottomLimit = top + height - labelReserved - insetBottom;

  if (SETTINGS.debugBorders) {
    // Same diagnostic outline the reader draws around its text viewport.
    renderer.drawRect(textLeft, y, textWidth, textBottomLimit - y);
  }

  // The smear is renderer state, so it must be cleared on every exit path below —
  // otherwise the tab bar, the row list and the button hints would render thickened too.
  struct PaperbackScope {
    const GfxRenderer& renderer;
    ~PaperbackScope() { renderer.setPaperbackLook(false); }
  } paperbackScope{renderer};

  // Draw the sample twice so the paragraph gap is visible. The first copy is always plain
  // regular weight and the second carries the Paperback Look ink smear when it is on, so
  // the two renders sit one above the other for comparison.
  for (int paragraph = 0; paragraph < 2; paragraph++) {
    renderer.setPaperbackLook(paragraph == 1 && SETTINGS.paperbackLookBody != 0);
    for (const auto& line : layout.lines) {
      if (y + lineH > textBottomLimit) return;
      line->render(renderer, fontId, textLeft, y);
      y += lineAdvance;
    }
    y += paragraphGap;
  }
}

}  // namespace textsettings
