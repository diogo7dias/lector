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
#include "components/UITheme.h"
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

// The sample's pretend publisher stylesheet: a centred chapter heading and a first-line
// indent on the body. Without it the Embedded Style toggle and First Line Indent: Book
// would have nothing to act on, because the sample carries no CSS of its own.
constexpr int SAMPLE_CSS_INDENT_EM_TENTHS = 15;  // 1.5em, a common publisher indent

// Placeholder values for the status bar. The pane shows a made-up page, so the bar shows
// made-up readings; only the anchors are real, and those decide how much of each vertical
// margin the bar eats.
constexpr char SAMPLE_BATTERY[] = "72%";
constexpr char SAMPLE_CLOCK[] = "12:34";
constexpr char SAMPLE_PAGE[] = "12/318";
constexpr char SAMPLE_PERCENT[] = "37%";

bool anchorIsTop(uint8_t anchor) {
  return anchor >= CrossPointSettings::SB_ANCHOR_TL && anchor <= CrossPointSettings::SB_ANCHOR_TR;
}

// 0 = left, 1 = centre, 2 = right, for both the top and the bottom row of anchors.
int anchorSlot(uint8_t anchor) { return (anchor - CrossPointSettings::SB_ANCHOR_TL) % 3; }

// Fills slots[3] with the sample text of every status bar item anchored to the requested
// edge, in the same left/centre/right slots the reader uses. Items sharing a slot are
// joined with a space, exactly as the bar itself packs them.
void collectStatusBarSlots(bool top, std::string slots[3]) {
  const struct {
    uint8_t anchor;
    const char* text;
  } items[] = {
      {SETTINGS.sbBatteryPos, SAMPLE_BATTERY},   {SETTINGS.sbClockPos, SAMPLE_CLOCK},
      {SETTINGS.sbPagePos, SAMPLE_PAGE},         {SETTINGS.sbBookPctPos, SAMPLE_PERCENT},
      {SETTINGS.sbChapterPctPos, SAMPLE_PERCENT}, {SETTINGS.sbChapterNumPos, "Ch 2/14"},
      {SETTINGS.sbSessionPagesPos, "+8"},
      {SETTINGS.sbParaPagesPos, ">P.0"},
  };
  for (const auto& item : items) {
    if (item.anchor == CrossPointSettings::SB_ANCHOR_OFF) continue;
    if (anchorIsTop(item.anchor) != top) continue;
    std::string& slot = slots[anchorSlot(item.anchor)];
    if (!slot.empty()) slot += " ";
    slot += item.text;
  }
  // The title is the one item with its own text, and it is the only one worth truncating.
  if (SETTINGS.sbTitlePos != CrossPointSettings::SB_ANCHOR_OFF && anchorIsTop(SETTINGS.sbTitlePos) == top) {
    std::string& slot = slots[anchorSlot(SETTINGS.sbTitlePos)];
    if (!slot.empty()) slot += " ";
    slot += I18N.get(StrId::STR_PREVIEW_HEADING);
  }
}

// Draws one edge of the status bar and returns the height it occupied, which is the part
// of the vertical margin the page does not get to use.
int drawStatusBarEdge(const GfxRenderer& renderer, bool top, int edgeY, int paneLeft, int paneWidth) {
  if (!SETTINGS.statusBarEnabled()) return 0;

  std::string slots[3];
  collectStatusBarSlots(top, slots);
  if (slots[0].empty() && slots[1].empty() && slots[2].empty()) return 0;

  const int lineH = renderer.getTextHeight(SMALL_FONT_ID);
  const int sideMargin = UITheme::getInstance().getMetrics().statusBarHorizontalMargin;
  const int y = top ? edgeY : edgeY - lineH;

  if (!slots[0].empty()) renderer.drawText(SMALL_FONT_ID, paneLeft + sideMargin, y, slots[0].c_str());
  if (!slots[1].empty()) {
    const int w = renderer.getTextWidth(SMALL_FONT_ID, slots[1].c_str());
    renderer.drawText(SMALL_FONT_ID, paneLeft + (paneWidth - w) / 2, y, slots[1].c_str());
  }
  if (!slots[2].empty()) {
    const int w = renderer.getTextWidth(SMALL_FONT_ID, slots[2].c_str());
    renderer.drawText(SMALL_FONT_ID, paneLeft + paneWidth - sideMargin - w, y, slots[2].c_str());
  }
  return lineH;
}

// Horizontal reading margin, resolved exactly like EpubReaderActivity::computeReaderMargins
// does for the page: Dynamic Margins replaces the fixed margin with a width aimed at ~62
// characters per line. Measured against the FULL screen, which is also the pane's width,
// so the margin drawn here is the margin the page will get.
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

// Feeds one space-separated word at a time; addWord handles NFC/CJK/RTL/focus splitting.
void addWords(ParsedText& parsed, const char* text, EpdFontFamily::Style style) {
  std::string word;
  for (const char* p = text;; p++) {
    if (*p == ' ' || *p == '\0') {
      if (!word.empty()) {
        parsed.addWord(word, style);
        word.clear();
      }
      if (*p == '\0') break;
    } else {
      word.push_back(*p);
    }
  }
}

BlockStyle bodyStyle(int fontId, const GfxRenderer& renderer) {
  BlockStyle style;
  style.alignment = toCssAlign(SETTINGS.paragraphAlignment);
  style.textAlignDefined = true;  // honor the user's choice; RTL auto-detected from text
  if (SETTINGS.embeddedLayoutStyle) {
    // The sample's own stylesheet. First Line Indent: Book defers to exactly this, so
    // without it that mode would look identical to a 0% custom indent.
    const int em = std::max(1, renderer.getTextHeight(fontId));
    style.textIndent = static_cast<int16_t>(em * SAMPLE_CSS_INDENT_EM_TENTHS / 10);
    style.textIndentDefined = true;
  }
  return style;
}

// Lays one paragraph out and appends its lines, the first of them carrying the gap that
// separates it from the paragraph above.
void appendParagraph(PreviewLayout& layout, const GfxRenderer& renderer, int fontId, int textWidth, const char* text,
                     const BlockStyle& style, bool heading, int gapBefore) {
  ParsedText parsed(SETTINGS.extraParagraphSpacing != 0, SETTINGS.hyphenationEnabled != 0,
                    SETTINGS.focusReadingEnabled != 0,
                    resolveGuideDotsMode(SETTINGS.guideDotsEnabled, SETTINGS.guideDotsHidden), style,
                    SETTINGS.firstLineIndentMode, SETTINGS.firstLineIndentPercent);
  parsed.setHeading(heading);
  addWords(parsed, text, heading ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

  bool first = true;
  parsed.layoutAndExtractLines(renderer, fontId, static_cast<uint16_t>(textWidth),
                               [&layout, &first, gapBefore](std::shared_ptr<TextBlock> line, uint32_t) {
                                 layout.lines.push_back({std::move(line), first ? gapBefore : 0});
                                 first = false;
                               });
}

// Lay the whole sample page out: pretend chapter heading (only while Embedded Layout Style
// is on, since it is the sample's CSS that puts it there), then two body paragraphs so the
// paragraph gap is visible and the bottom of the page holds different text from the top.
void relayout(PreviewLayout& layout, const GfxRenderer& renderer, int fontId, int textWidth, int lineAdvance,
              int paragraphGap) {
  layout.lines.clear();
  layout.secondParagraphLine = 0;

  const BlockStyle body = bodyStyle(fontId, renderer);
  if (SETTINGS.embeddedLayoutStyle) {
    BlockStyle heading;
    heading.alignment = CssTextAlign::Center;
    heading.textAlignDefined = true;
    appendParagraph(layout, renderer, fontId, textWidth, I18N.get(StrId::STR_PREVIEW_HEADING), heading, true, 0);
    appendParagraph(layout, renderer, fontId, textWidth, I18N.get(StrId::STR_FONT_PREVIEW_TEXT), body, false,
                    lineAdvance / 2);
  } else {
    appendParagraph(layout, renderer, fontId, textWidth, I18N.get(StrId::STR_FONT_PREVIEW_TEXT), body, false, 0);
  }
  layout.secondParagraphLine = static_cast<int>(layout.lines.size());
  appendParagraph(layout, renderer, fontId, textWidth, I18N.get(StrId::STR_PREVIEW_TEXT_2), body, false, paragraphGap);
}

}  // namespace

void renderPreview(const GfxRenderer& renderer, PreviewLayout& layout, const int top, const int height) {
  const int paneLeft = 0;
  const int paneWidth = renderer.getScreenWidth();
  if (paneWidth <= 0 || height <= 0) return;

  const int fontId = SETTINGS.getReaderFontId();
  if (fontId == 0) return;
  const int lineH = renderer.getTextHeight(fontId);
  if (lineH <= 0) return;

  const int marginH = resolveHorizontalMargin(renderer, fontId);
  const int textLeft = paneLeft + marginH;
  const int textWidth = paneWidth - 2 * marginH;
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
                       .embeddedLayoutStyle = SETTINGS.embeddedLayoutStyle != 0,
                       .paragraphSpacing = SETTINGS.paragraphSpacing,
                       .guideDotsMode = resolveGuideDotsMode(SETTINGS.guideDotsEnabled, SETTINGS.guideDotsHidden),
                       .firstLineIndentMode = SETTINGS.firstLineIndentMode,
                       .firstLineIndentPercent = SETTINGS.firstLineIndentPercent};
  if (key != layout.key) {
    if (auto* fcm = renderer.getFontCacheManager()) {
      // The guide dot is not in the sample sentence, so it has to be prewarmed
      // explicitly or an SD font would miss the glyph on the first draw.
      std::string prewarmText = I18N.get(StrId::STR_FONT_PREVIEW_TEXT);
      prewarmText += I18N.get(StrId::STR_PREVIEW_TEXT_2);
      prewarmText += I18N.get(StrId::STR_PREVIEW_HEADING);
      if (SETTINGS.guideDotsEnabled) prewarmText += GUIDE_DOT_UTF8;
      // Bit 1 is the bold mask the heading needs; bit 0 the regular body.
      uint8_t styleMask = SETTINGS.focusReadingEnabled ? 0x03 : 0x01;
      if (SETTINGS.embeddedLayoutStyle) styleMask |= 0x02;
      fcm->prewarmCache(fontId, prewarmText.c_str(), styleMask);
    }
    relayout(layout, renderer, fontId, textWidth, lineAdvance, paragraphGap);
    layout.key = key;
  }
  if (layout.lines.empty()) return;

  const int topBarHeight = drawStatusBarEdge(renderer, /*top=*/true, top, paneLeft, paneWidth);

  // The smear is renderer state, so it must be cleared on every exit path below —
  // otherwise the row grid and the button hints would render thickened too.
  struct PaperbackScope {
    const GfxRenderer& renderer;
    ~PaperbackScope() { renderer.setPaperbackLook(false); }
  } paperbackScope{renderer};

  // First paragraph with the smear, second without it. No labels at the seam: the
  // paragraph gap is the seam, and a label would cost a line of the passage being judged.
  const int textTop = top + topBarHeight + SETTINGS.screenMarginTop;
  const int textLimit = top + height;
  int y = textTop;
  int drawn = 0;
  renderer.setPaperbackLook(true);
  for (const auto& entry : layout.lines) {
    if (drawn == layout.secondParagraphLine) renderer.setPaperbackLook(false);
    y += entry.gapBefore;
    if (y + lineH > textLimit) break;
    entry.line->render(renderer, fontId, textLeft, y);
    y += lineAdvance;
    drawn++;
  }
  renderer.setPaperbackLook(false);
  if (SETTINGS.debugBorders && y > textTop) renderer.drawRect(textLeft, textTop, textWidth, y - textTop);
}

}  // namespace textsettings
