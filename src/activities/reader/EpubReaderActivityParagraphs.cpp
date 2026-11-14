#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "DictionaryHistoryActivity.h"
#include "DictionaryWordSelectActivity.h"
#include "EpubReaderActivity.h"
#include "ParagraphNumberLayout.h"
#include "QuoteSelectActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "fontIds.h"

int EpubReaderActivity::findPageForOrdinal(Section& sec, const uint16_t ordinal, bool* const outFound) const {
  if (outFound) *outFound = false;
  const int pages = sec.pageCount;
  for (int p = 0; p < pages; p++) {
    auto page = sec.loadPage(p);
    if (!page) continue;
    for (const auto& el : page->elements) {
      if (el->getTag() != TAG_PageLine) continue;
      // Ordinals are contiguous and increase down the chapter, so the first page whose
      // lines reach the target holds that paragraph's first line.
      if (static_cast<const PageLine&>(*el).getParagraphOrdinal() >= ordinal) {
        if (outFound) *outFound = true;
        return p;
      }
    }
  }
  // Past the last paragraph laid out so far. On a finished chapter that means the last
  // page; on a chapter still building it means "not reached yet", which `outFound`
  // tells the caller apart so it can wait rather than land on the build watermark.
  return pages > 0 ? pages - 1 : 0;
}

std::optional<uint16_t> EpubReaderActivity::firstOrdinalOnPage(Section& sec, const int page) const {
  if (page < 0 || page >= sec.pageCount) return std::nullopt;
  const auto loaded = sec.loadPage(page);
  if (!loaded) return std::nullopt;
  // Only the FIRST line of a paragraph carries its ordinal; every other line is 0. So a
  // page that opens in the middle of a paragraph shows 0 until the next paragraph starts,
  // and the paragraph the page opened in is the one before that. That is the paragraph
  // the reader was looking at, which is what the anchor wants.
  bool openedMidParagraph = false;
  bool sawFirstLine = false;
  for (const auto& el : loaded->elements) {
    if (el->getTag() != TAG_PageLine) continue;
    const uint16_t ordinal = static_cast<const PageLine&>(*el).getParagraphOrdinal();
    if (!sawFirstLine) {
      sawFirstLine = true;
      openedMidParagraph = (ordinal == 0);
    }
    if (ordinal == 0) continue;
    if (!openedMidParagraph) return ordinal;  // page starts on a paragraph
    return (ordinal > 1) ? static_cast<uint16_t>(ordinal - 1) : ordinal;
  }
  // No paragraph starts on this page at all: it sits wholly inside one long paragraph,
  // and there is nothing here to name it by.
  return std::nullopt;
}

void EpubReaderActivity::captureOrdinalAnchor() {
  if (!section) return;
  // The anchor is the first paragraph that *starts* on this page. A paragraph running
  // across the page boundary from above resolves to its own first page, so the reader
  // can land slightly earlier than it was, never later. Earlier is the safe way to be
  // wrong: re-reading a line costs nothing, skipping one is a spoiler.
  if (const auto ordinal = firstOrdinalOnPage(*section, section->currentPage)) {
    pendingOrdinalAnchor_ = *ordinal;
  }
}

void EpubReaderActivity::awaitParagraphInBuild(const uint16_t ordinal) {
  // The chapter is not laid out that far yet, so the scan landed on the build watermark.
  // Hand the paragraph to the relayout anchor, which moves the reader once the build
  // reaches it (render) or finishes (applyDeferredReposition).
  pendingOrdinalAnchor_ = ordinal;
  cachedSpineIndex = currentSpineIndex;
}

void EpubReaderActivity::jumpToParagraph(const int target) {
  if (!epub || target < 1) {
    requestUpdate();
    return;
  }

  // Numbering restarts at 1 in every chapter, so the number typed is always a paragraph
  // of the chapter being read. There is no book-wide range to search. Clamped, not cast:
  // a six-digit entry used to wrap past 65535 to an early paragraph.
  const uint16_t localOrdinal = static_cast<uint16_t>(std::min(target, static_cast<int>(UINT16_MAX)));

  {
    // The same preamble as every other jump: Return can undo it, and a deferred
    // reposition from a background build cannot snap the page back afterwards.
    RenderLock lock(*this);
    recordJumpOrigin();
    clearDeferredReposition();
    if (section) {
      bool found = false;
      const int page = findPageForOrdinal(*section, localOrdinal, &found);
      section->currentPage = page;
      nextPageNumber = page;
      if (!found && !section->isBuildComplete()) awaitParagraphInBuild(localOrdinal);
    } else {
      // Section not loaded yet: defer the page scan until it is.
      nextPageNumber = 0;
      pendingParagraphScan_ = localOrdinal;
    }
  }
  requestUpdate();
}

void EpubReaderActivity::drawParagraphNumbers(const Page& page, const int marginLeft, const int marginTop,
                                              const int fontId) {
  if (prefs_.paragraphNumbering == CrossPointSettings::PARA_NUM_OFF) return;
  constexpr int kNumFontIds[] = {PARA_NUM_13_FONT_ID, PARA_NUM_26_FONT_ID, PARA_NUM_39_FONT_ID};
  const int numFontId = kNumFontIds[paragraphNumberScale(prefs_.paragraphNumberSize) - 1];
  int viewTop = 0, viewRight = 0, viewBottom = 0, pageLeft = 0;
  renderer.getOrientedViewableTRBL(&viewTop, &viewRight, &viewBottom, &pageLeft);
  const int lineHeight = renderer.getLineHeight(fontId);
  // Sit the number on the same optical line as the words, at any reading size. The rule
  // itself lives in ParagraphNumberLayout.h so the host tests can exercise it; the lookups
  // are hoisted here so they cost one glyph query per page rather than one per line.
  ParagraphNumberMetrics metrics;
  metrics.bodyAscender = renderer.getFontAscenderSize(fontId);
  metrics.bodyLineHeight = lineHeight;
  metrics.numAscender = renderer.getFontAscenderSize(numFontId);
  metrics.numLineHeight = renderer.getLineHeight(numFontId);
  // 'x' because lowercase is the bulk of what a line of prose looks like; 'H' stands in
  // for a face without it, and the layout falls back to line-box centring without either.
  metrics.bodyInkTop = renderer.getGlyphInkTop(fontId, 'x');
  if (metrics.bodyInkTop <= 0) metrics.bodyInkTop = renderer.getGlyphInkTop(fontId, 'H');
  metrics.numInkTop = renderer.getGlyphInkTop(numFontId, '0');
  for (const auto& el : page.elements) {
    if (el->getTag() != TAG_PageLine) continue;
    const auto& line = static_cast<const PageLine&>(*el);
    const uint16_t ord = line.getParagraphOrdinal();
    if (ord == 0) continue;  // not a paragraph's first line
    const auto& block = line.getBlock();
    if (!block || block->wordCount() == 0) continue;
    char buf[12];
    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(ord));
    const int numWidth = renderer.getTextWidth(numFontId, buf);
    // wordXpos(0) is non-zero for indented, centred and RTL lines, so the room the
    // number has runs to the first letter, not just to the text column.
    const int x = paragraphNumberX(pageLeft, numWidth, marginLeft + line.xPos + block->wordXpos(0));
    if (x < 0) continue;
    metrics.lineTop = marginTop + line.yPos;
    const int y = paragraphNumberDrawY(metrics);
    renderer.drawText(numFontId, x, y, buf, true);
  }
}
