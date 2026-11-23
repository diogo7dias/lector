#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_system.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "DictionaryWordSelectActivity.h"
#include "EpubReaderActivity.h"
#include "MappedInputManager.h"
#include "QuoteSelectActivity.h"
#include "ReaderMargins.h"
#include "ReaderUtils.h"
#include "RuledLines.h"
#include "SdCardFontSystem.h"
#include "activities/network/NearbyFileTransferActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "util/ReadingPercent.h"
#include "util/SortesSelection.h"

void EpubReaderActivity::renderContents(std::unique_ptr<Page> page, const int orientedMarginTop,
                                        const int orientedMarginRight, const int orientedMarginBottom,
                                        const int orientedMarginLeft) {
  const auto t0 = millis();
  const int fontId = SETTINGS.getReaderFontId(prefs_);

  // The image pixel-cache RAM slot lives for exactly one page render (it feeds
  // the BW double-refresh and every grayscale band pass); release it on every
  // exit so nothing stays resident across page turns.
  struct PxcSlotGuard {
    ~PxcSlotGuard() { ImageBlock::releaseRenderCache(); }
  } pxcSlotGuard;

  // Font prewarm: scan pass accumulates text, then prewarm, then real render
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);  // scan pass
  // Scan the status bar too: a CJK book/chapter title redirected to the SD
  // fallback font joins the page's single batch prewarm instead of triggering
  // its own SD pass after the scope ends.
  renderStatusBar();
  scope.endScanAndPrewarm();
  const auto tPrewarm = millis();

  // Images: Display (B/W): the page turns as plain B/W, images dither in the B/W pass and
  // text keeps its anti-aliasing. The layout is the same as Display, so only the draw differs.
  const bool imagesInBw = prefs_.imageRendering == CrossPointSettings::IMAGES_DISPLAY_BW;
  struct DitherScope {
    GfxRenderer& renderer;
    ~DitherScope() { renderer.setDitherImagesInBw(false); }
  } ditherScope{renderer};
  renderer.setDitherImagesInBw(imagesInBw);
  const bool pageHasImages = !imagesInBw && page->hasImages();
  const bool pageHasImagesNeedingDecode = pageHasImages && page->hasImagesNeedingDecode();
  const bool manualRefreshPending = forcedRefreshPending;
  forcedRefreshPending = false;
  // The reader starts with zero here, which means the normal refresh cycle
  // would use a HALF refresh for its first page. Keep that same clean base for
  // image pages: their double-FAST path otherwise runs directly over the
  // retained frame after a silent restart (for example, when returning from
  // KOReader sync), leaving the old UI mixed with the image.
  const bool cleanImageBasePending = manualRefreshPending || pagesUntilFullRefresh <= 1;
  // Text is never anti-aliased (decided 2026-11-22: the grey pass was 467 ms of a 1215 ms
  // turn), so only an image page takes a grey pass.
  const bool tiledGrayscale = pageHasImages && renderer.supportsStripGrayscale();
  auto renderGrayscalePass = [&]() { page->renderImages(renderer, fontId, orientedMarginLeft, orientedMarginTop); };

  if (pageHasImagesNeedingDecode) {
    page->renderWithImagePlaceholders(renderer, fontId, orientedMarginLeft, orientedMarginTop);
    renderStatusBar();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    renderer.clearScreen();
  }

  // Paperback Look (body): thicken the page glyphs on the BW base pass, then reset so
  // the paragraph numbers, the status bar (its own flag) and any grayscale/overlay
  // passes render thin. Image pages keep the thick text: the smear pixels set here
  // persist through the image-area re-blank/re-render below.
  renderer.setPaperbackLook(prefs_.paperbackLookBody);
  page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);
  renderer.setPaperbackLook(false);
  // Before the quote underlines, which would otherwise read as ink and cut the rule.
  ruled_lines::drawPage(renderer, *page, fontId, SETTINGS.getReaderLineCompression(prefs_), orientedMarginLeft,
                        orientedMarginTop, orientedMarginRight, orientedMarginBottom);
  drawParagraphNumbers(*page, orientedMarginLeft, orientedMarginTop, fontId);
  drawQuoteUnderlines(*page, orientedMarginLeft, orientedMarginTop, fontId);
  if (SETTINGS.debugBorders) {
    // Diagnostic: outline the text viewport on the BW base plane. Draw-only overlay,
    // never affects layout or the section cache.
    const int vpW = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
    const int vpH = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;
    renderer.drawRect(orientedMarginLeft, orientedMarginTop, vpW, vpH);
  }
  renderStatusBar();
  const auto tBwRender = millis();

  if (pageHasImages) {
    // Match CrossPoint: image pages use one base refresh before the grayscale pass.
    // FAST leaves the panel receptive to the gray waveform; pending cleanup still honors
    // the scheduled/manual HALF refresh.
    renderer.displayBuffer(cleanImageBasePending ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
    // The grayscale pass below leaves gray charge in the image region that a plain
    // fast diff on the next page can't clear, so force the next ordinary page onto the
    // HALF ghost-cleanup path.
    pagesUntilFullRefresh = 0;
  } else {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  }
  const auto tDisplay = millis();

  // Tiled grayscale for the images: render each plane band-by-band, leaving the BW
  // framebuffer intact so no full-frame storeBwBuffer is needed; controller RAM is
  // re-synced from the live framebuffer afterward.
  if (tiledGrayscale) {
    constexpr int STRIP_ROWS = 80;
    const int gh = renderer.getDisplayHeight();
    const int gwBytes = renderer.getDisplayWidthBytes();
    // The strip writes below need the panel idle.
    uint8_t* scratch = acquireGrayscaleStripScratch(static_cast<size_t>(gwBytes) * STRIP_ROWS);
    renderer.waitRefreshComplete();
    if (!scratch) {
      LOG_ERR("ERS", "OOM: grayscale strip scratch (%d bytes); skipping the image greys this page",
              gwBytes * STRIP_ROWS);
    } else {
      // Bands may be streamed in any order: X4 windows each via setRamArea,
      // X3 via PTL.
      renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
      for (int y = 0; y < gh; y += STRIP_ROWS) {
        const int rows = (gh - y < STRIP_ROWS) ? (gh - y) : STRIP_ROWS;
        renderer.beginStripTarget(scratch, y, rows);
        renderer.clearScreen(0x00);
        renderGrayscalePass();
        renderer.endStripTarget();
        renderer.writeGrayscalePlaneStrip(true, scratch, y, rows);
      }
      const auto tGrayLsb = millis();

      // MSB plane.
      renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
      for (int y = 0; y < gh; y += STRIP_ROWS) {
        const int rows = (gh - y < STRIP_ROWS) ? (gh - y) : STRIP_ROWS;
        renderer.beginStripTarget(scratch, y, rows);
        renderer.clearScreen(0x00);
        renderGrayscalePass();
        renderer.endStripTarget();
        renderer.writeGrayscalePlaneStrip(false, scratch, y, rows);
      }
      const auto tGrayMsb = millis();

      renderer.setRenderMode(GfxRenderer::BW);
      renderer.displayGrayBuffer();
      const auto tGrayDisplay = millis();

      // BW framebuffer is intact; re-sync controller RAM for the next
      // differential page turn directly from it.
      renderer.cleanupGrayscaleWithFrameBuffer();
      const auto tCleanup = millis();

      const auto tEnd = millis();
      LOG_DBG("ERS",
              "Page render (tiled): prewarm=%lums bw_render=%lums display=%lums gray_lsb=%lums "
              "gray_msb=%lums gray_display=%lums cleanup=%lums total=%lums",
              tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tGrayLsb - tDisplay, tGrayMsb - tGrayLsb,
              tGrayDisplay - tGrayMsb, tCleanup - tGrayDisplay, tEnd - t0);
    }
  } else {
    // Fallback path for a controller without strip support: the whole-frame store/restore
    // pass. Only an image page takes it.
    if (pageHasImages && !ReaderUtils::renderAntiAliased(renderer, renderGrayscalePass)) {
      LOG_ERR("ERS", "Skipping grayscale this page");
    }
    const auto tEnd = millis();
    LOG_DBG("ERS", "Page render: prewarm=%lums bw_render=%lums display=%lums gray=%lums total=%lums", tPrewarm - t0,
            tBwRender - tPrewarm, tDisplay - tBwRender, tEnd - tDisplay, tEnd - t0);
  }
}

void EpubReaderActivity::renderStatusBar() const {
  StatusBarData d;
  if (!sortesMode) d.sessionPages = sessionPages;
  d.hasChapters = true;  // EPUB spine sections + TOC always provide chapters
  d.chapterPage = section->currentPage + 1;
  // estimatedTotalPages() keeps "page X / Y" sane while a giant spine is still
  // building (its live pageCount would read off the small build watermark).
  d.chapterPages = static_cast<int>(section->estimatedTotalPages());
  d.chapterPercent = reading_percent::pagePercent(section->currentPage, d.chapterPages);
  d.bookPercent = bookPercent();
  d.chapterMarks = chapterMarks_;
  d.chapterMarkCount = chapterMarkCount_;
  d.bookTitle = epub->getTitle();
  d.chapterTotal = epub->getTocItemsCount();
  d.printedPage = printedPageLabel();

  const ChapterLabel chapter = currentChapterLabel();
  if (chapter.tocIndex != -1) {
    d.chapterTitle = chapter.title;
    d.chapterNum = chapter.tocIndex + 1;
  }
  if (d.chapterTitle.empty()) d.chapterTitle = tr(STR_UNNAMED);
  // Read from the chapter's paragraph table, and only when the item is on: it costs
  // one section-file open, which is not worth paying on every page for a number
  // nobody asked to see.
  const StatusBarBlock sb = statusBarOf(prefs_);
  if (sb.paraPagesPos != CrossPointSettings::SB_ANCHOR_OFF) {
    if (const auto left = section->pagesUntilNextParagraph(section->currentPage)) {
      d.paragraphPagesLeft = static_cast<int>(*left);
    }
  }
  // Paperback Look (status bar): thicken only the status-bar glyphs, then reset so
  // nothing drawn afterwards inherits the smear.
  renderer.setPaperbackLook(prefs_.paperbackLookStatus);
  GUI.drawStatusBarV2(renderer, d, sb);
  renderer.setPaperbackLook(false);
}

const char* EpubReaderActivity::printedPageLabel() const {
  if (!hasPageList_ || !section) return nullptr;
  // Resolving anchors opens the section's anchor map, so this runs once per section and once
  // more when its build settles (anchors past the build watermark are unknown until then).
  if (printedMarksStale_ || section->isBuilding() != printedMarksBuilding_) {
    printedMarksStale_ = false;
    printedMarksBuilding_ = section->isBuilding();
    printedMarks_.clear();
    printedMarks_.reserve(32);
    struct Ctx {
      const EpubReaderActivity& self;
    } ctx{*this};
    epub->forEachPrintedPage(
        [](void* c, const std::string_view label, const char* href) {
          const auto& self = static_cast<Ctx*>(c)->self;
          if (label.size() >= sizeof(PrintedMark::label)) return true;
          if (self.epub->resolveHrefToSpineIndex(href) != self.currentSpineIndex) return true;
          const char* hash = strchr(href, '#');
          std::optional<uint16_t> page = 0;
          if (hash && hash[1] != '\0') page = self.section->findAnchor(hash + 1);
          if (!page) return true;
          PrintedMark mark{*page, {}};
          memcpy(mark.label, label.data(), label.size());
          self.printedMarks_.push_back(mark);
          return true;
        },
        &ctx);
  }
  // The last marker on or before this page: the printed page this one belongs to.
  const char* label = nullptr;
  uint16_t best = 0;
  for (const auto& mark : printedMarks_) {
    if (mark.page <= section->currentPage && (!label || mark.page >= best)) {
      label = mark.label;
      best = mark.page;
    }
  }
  return label;
}
