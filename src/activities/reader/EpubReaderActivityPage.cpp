#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
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

  const bool pageHasImages = page->hasImages();
  const bool pageHasImagesNeedingDecode = pageHasImages && page->hasImagesNeedingDecode();
  const bool manualRefreshPending = forcedRefreshPending;
  forcedRefreshPending = false;
  // The reader starts with zero here, which means the normal refresh cycle
  // would use a HALF refresh for its first page. Keep that same clean base for
  // image pages: their double-FAST path otherwise runs directly over the
  // retained frame after a silent restart (for example, when returning from
  // KOReader sync), leaving the old UI mixed with the image.
  const bool cleanImageBasePending = manualRefreshPending || pagesUntilFullRefresh <= 1;
  // Per-book, not global: a book carrying its own reader settings decides this.
  const bool needsTextGrayscale = prefs_.textAntiAliasing;
  const bool needsAnyGrayscale = needsTextGrayscale || pageHasImages;
  const bool tiledGrayscale = needsAnyGrayscale && renderer.supportsStripGrayscale();
  // Whole-plane buffering only pays when the BW refresh genuinely runs async
  // underneath it; on blocking panels (X3) it would just spend ~50 KB for the
  // identical serial timing. Image pages take the blocking double-FAST path
  // below (no async refresh is ever started), so they'd spend the buffers with
  // nothing in flight to overlap.
  const bool overlapRefresh = tiledGrayscale && renderer.supportsAsyncRefresh() && !pageHasImages;
  auto renderGrayscalePass = [&]() {
    if (needsTextGrayscale) {
      page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);
    } else {
      page->renderImages(renderer, fontId, orientedMarginLeft, orientedMarginTop);
    }
  };

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
    // Double FAST_REFRESH with selective image blanking (pablohc's technique):
    // HALF_REFRESH sets particles too firmly for the grayscale LUT to adjust.
    // Instead, blank only the image area and do two fast refreshes.
    // Step 1: Display page with image area blanked (text appears, image area white)
    // Step 2: Re-render with images and display again (images appear clean)
    int16_t imgX, imgY, imgW, imgH;
    if (page->getImageBoundingBox(imgX, imgY, imgW, imgH)) {
      // Image pages intentionally bypass the regular refresh cadence. Preserve
      // a pending clean base before their double-FAST grayscale pipeline.
      if (cleanImageBasePending) {
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      }
      renderer.fillRect(imgX + orientedMarginLeft, imgY + orientedMarginTop, imgW, imgH, false);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);

      // Re-render page content to restore images into the blanked area
      // Status bar is not re-rendered here to avoid reading stale dynamic values (e.g. battery %)
      page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    } else {
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
    // The image's own page is handled above and doesn't count toward the full
    // refresh cadence. But the grayscale pass below leaves gray charge in the
    // image region that a plain fast diff on the *next* page can't clear, so
    // text there ghosts gray (#2190). Force the next ordinary page onto the
    // HALF ghost-cleanup path, which drives every pixel to its target
    // regardless of residue.
    pagesUntilFullRefresh = 0;
  } else {
    const bool cleanupRefresh = pagesUntilFullRefresh <= 1;
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh, overlapRefresh);
    // X3 AA needs its grayscale-precondition waveform after a HALF cleanup
    // before gray planes are written. No-op on other panels. Skip when the
    // BW refresh went out async — waitRefreshComplete below is the settle.
    if (needsAnyGrayscale && cleanupRefresh && !overlapRefresh) {
      renderer.preconditionGrayscale();
    }
  }
  const auto tDisplay = millis();

  // Tiled grayscale: render each plane band-by-band, leaving the BW
  // framebuffer intact so no full-frame storeBwBuffer is needed; controller
  // RAM is re-synced from the live framebuffer afterward. The page is
  // re-rendered ceil(H/STRIP_ROWS) times per plane, but renderCharImpl culls
  // out-of-band glyphs before decode so the cost stays close to one render.
  // Both text (drawPixel) and images (DirectPixelWriter) honor the active
  // strip target. When the BW refresh above went out async, the plane
  // rendering below overlaps the panel's refresh time; only the controller
  // RAM writes wait for BUSY.
  if (tiledGrayscale) {
    constexpr int STRIP_ROWS = 80;
    const int gh = renderer.getDisplayHeight();
    const int gwBytes = renderer.getDisplayWidthBytes();
    const size_t planeBytes = static_cast<size_t>(gwBytes) * gh;

    // Render one plane band-by-band into a whole-plane buffer without touching
    // the controller, so it can run while the refresh is still in flight.
    auto renderPlaneToBuffer = [&](const bool lsbPlane, uint8_t* buf) {
      renderer.setRenderMode(lsbPlane ? GfxRenderer::GRAYSCALE_LSB : GfxRenderer::GRAYSCALE_MSB);
      for (int y = 0; y < gh; y += STRIP_ROWS) {
        const int rows = (gh - y < STRIP_ROWS) ? (gh - y) : STRIP_ROWS;
        renderer.beginStripTarget(buf + static_cast<size_t>(y) * gwBytes, y, rows);
        renderer.clearScreen(0x00);
        renderGrayscalePass();
        renderer.endStripTarget();
      }
    };

    // Tiered on heap pressure: two plane buffers hide both plane renders
    // inside the refresh wait; one hides the LSB render (its buffer is reused
    // for MSB after streaming); none falls back to the strip-scratch flow with
    // no overlap. Each buffer is only attempted when it leaves ~60 KB free so
    // the pass never starves concurrent allocations: the next page re-render
    // allocates through throwing std::string paths that abort() on OOM under
    // -fno-exceptions, so a plane buffer that "fits" but eats the render
    // headroom is worse than the strip fallback. Blocking panels skip the
    // buffers entirely (nothing to overlap).
    constexpr size_t PLANE_BUF_HEADROOM = 60000;
    // Free-heap alone ignores fragmentation: taking the largest block for a
    // plane can leave only slivers behind even when total headroom looks fine.
    // Require the block to fit the plane with 16 KB contiguous to spare, which
    // also keeps the advance-table batch scratch viable mid-render (same
    // rationale as BACKGROUND_BUILD_MIN_MAX_ALLOC).
    constexpr size_t PLANE_BUF_MAX_ALLOC_RESERVE = 16 * 1024;
    const auto planeBufFits = [planeBytes] {
      return ESP.getFreeHeap() >= planeBytes + PLANE_BUF_HEADROOM &&
             ESP.getMaxAllocHeap() >= planeBytes + PLANE_BUF_MAX_ALLOC_RESERVE;
    };
    auto lsbPlaneBuf = (overlapRefresh && planeBufFits()) ? makeUniqueNoThrow<uint8_t[]>(planeBytes) : nullptr;
    auto msbPlaneBuf = (lsbPlaneBuf && planeBufFits()) ? makeUniqueNoThrow<uint8_t[]>(planeBytes) : nullptr;

    if (lsbPlaneBuf) {
      renderPlaneToBuffer(true, lsbPlaneBuf.get());
      if (msbPlaneBuf) renderPlaneToBuffer(false, msbPlaneBuf.get());
      const auto tGrayRender = millis();

      renderer.waitRefreshComplete();
      const auto tWait = millis();

      renderer.writeGrayscalePlaneStrip(true, lsbPlaneBuf.get(), 0, gh);
      if (msbPlaneBuf) {
        renderer.writeGrayscalePlaneStrip(false, msbPlaneBuf.get(), 0, gh);
      } else {
        renderPlaneToBuffer(false, lsbPlaneBuf.get());
        renderer.writeGrayscalePlaneStrip(false, lsbPlaneBuf.get(), 0, gh);
      }
      const auto tGrayWrite = millis();

      renderer.setRenderMode(GfxRenderer::BW);
      renderer.displayGrayBuffer();
      const auto tGrayDisplay = millis();

      // BW framebuffer is intact; re-sync controller RAM for the next
      // differential page turn directly from it.
      renderer.cleanupGrayscaleWithFrameBuffer();
      const auto tEnd = millis();

      LOG_DBG("ERS",
              "Page render (tiled async): prewarm=%lums bw_render=%lums display=%lums gray_render=%lums "
              "wait=%lums gray_write=%lums gray_display=%lums cleanup=%lums total=%lums (planes buffered: %d)",
              tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tGrayRender - tDisplay, tWait - tGrayRender,
              tGrayWrite - tWait, tGrayDisplay - tGrayWrite, tEnd - tGrayDisplay, tEnd - t0, msbPlaneBuf ? 2 : 1);
    } else {
      // Per-strip scratch tier: blocking panels (X3) and the OOM fallback.
      // The strip writes below need the panel idle, so wait out any pending
      // async refresh first (no-op on blocking panels).
      uint8_t* scratch = acquireGrayscaleStripScratch(static_cast<size_t>(gwBytes) * STRIP_ROWS);
      renderer.waitRefreshComplete();
      if (!scratch) {
        LOG_ERR("ERS", "OOM: grayscale strip scratch (%d bytes); skipping AA this page", gwBytes * STRIP_ROWS);
        if (overlapRefresh) {
          // The BW refresh ran the shadow-free async path, so controller RAM's
          // differential baseline was never rebuilt. Even with AA skipped it must
          // be re-synced from the intact BW framebuffer, or the next differential
          // update diffs against stale contents.
          renderer.cleanupGrayscaleWithFrameBuffer();
        }
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
    }
  } else {
    // Fallback path for a controller without strip support: the whole-frame store/restore
    // pass every reader shares. Only runs when grayscale actually renders.
    // TODO: Only do this if font supports it
    if (needsAnyGrayscale && !ReaderUtils::renderAntiAliased(renderer, renderGrayscalePass)) {
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

  const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (tocIndex != -1) {
    d.chapterTitle = epub->getTocItem(tocIndex).title;
    d.chapterNum = tocIndex + 1;
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
  if (sb.chapterTimePos != CrossPointSettings::SB_ANCHOR_OFF || sb.bookTimePos != CrossPointSettings::SB_ANCHOR_OFF) {
    const int32_t msPerPage = pageTimer.msPerPage();
    const int chapterLeft = std::max(0, d.chapterPages - d.chapterPage);
    d.chapterMinutesLeft = reading_time::minutesFor(chapterLeft, msPerPage);
    const int bookLeft =
        reading_time::bookPagesLeft(chapterLeft, d.chapterPages, epub->calculateProgress(currentSpineIndex, 0.0f),
                                    epub->calculateProgress(currentSpineIndex, 1.0f));
    d.bookMinutesLeft = reading_time::minutesFor(bookLeft, msPerPage);
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
