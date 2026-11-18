#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_system.h>

#include "CrossPointSettings.h"
#include "DictionaryWordSelectActivity.h"
#include "EpubReaderActivity.h"
#include "EpubReaderUtils.h"
#include "IdlePrewarmNeighbour.h"
#include "MappedInputManager.h"
#include "QuoteSelectActivity.h"
#include "WatermarkBuildBail.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "util/SortesSelection.h"

void EpubReaderActivity::runIdleBackgroundWork() {
  // Idle glyph prewarm for the neighbour the reader is moving toward. The scan
  // pass draws nothing (FCM scan mode suppresses pixels), so the displayed
  // framebuffer is untouched; endScanAndPrewarm loads only glyphs not already
  // cached. Debounced past rapid page-flipping, one attempt per position, and
  // deferred while a render/build owns the CPU or the heap is at the render
  // floor. Cross-chapter prewarm is deliberately out of scope (next spine's
  // section isn't loaded).
  // A build tick holds the RenderLock for up to hundreds of ms, and input is only
  // sampled between loop passes, so a press made inside one is never seen. Every
  // background tick below waits while input is active; the input itself is never held.
  const bool inputActive = mappedInput.isInputActive();

  constexpr unsigned long IDLE_PREWARM_DEBOUNCE_MS = 400;
  if (!inputActive && section && !section->isBuilding() && !RenderLock::peek() && renderer.hasFrameBuffer() &&
      lastRenderCompleteMs != 0 && millis() - lastRenderCompleteMs > IDLE_PREWARM_DEBOUNCE_MS &&
      ESP.getFreeHeap() > RENDER_MIN_FREE_HEAP && ESP.getMaxAllocHeap() > BACKGROUND_BUILD_MIN_MAX_ALLOC &&
      (idlePrewarmSpine != currentSpineIndex || idlePrewarmPage != section->currentPage)) {
    RenderLock lock;                  // the page table must not change under the scan
    HalPowerManager::Lock fullClock;  // the idle clock drop may already have happened
    // Re-check under the lock: peek() and acquisition are not atomic, so the render
    // task may have reset/replaced the section or moved the page in between.
    // cppcheck-suppress knownConditionTrueFalse ; the render task changes section between the checks
    if (section && !section->isBuilding() &&
        (idlePrewarmSpine != currentSpineIndex || idlePrewarmPage != section->currentPage)) {
      const int neighbour = idlePrewarmNeighbour(section->currentPage, idlePrewarmPage, idlePrewarmSpine,
                                                 currentSpineIndex, static_cast<int>(section->pageCount));
      idlePrewarmSpine = currentSpineIndex;
      idlePrewarmPage = section->currentPage;
      if (neighbour >= 0) {
        if (const auto p = section->loadPage(neighbour)) {
          if (auto* fcm = renderer.getFontCacheManager()) {
            const auto t0 = millis();
            auto scope = fcm->createPrewarmScope();
            p->render(renderer, SETTINGS.getReaderFontId(prefs_), 0, 0);  // scan only, no pixels
            scope.endScanAndPrewarmSdFonts();
            LOG_DBG("ERS", "Idle prewarm: page %d in %lums", neighbour, millis() - t0);
          }
        }
      }
    }
  }

  if (progressSaveDue && !inputActive && !RenderLock::peek() && lastRenderCompleteMs != 0 &&
      millis() - lastRenderCompleteMs > IDLE_PREWARM_DEBOUNCE_MS) {
    RenderLock lock;  // saveProgress reads the section the render task replaces
    HalPowerManager::Lock fullClock;
    progressSaveDue = false;
    flushQueuedProgress();
  }

  // Lazily resume a partial's extension build once the reader nears its watermark. Far from
  // it the rebuild is all cost (whole-chapter re-layout from page 0) and no benefit this
  // session, so reopening a partial deliberately does NOT start it (see the deferral in
  // render()); crossing this margin is the signal that the reader will actually need pages
  // past the watermark soon. Uses the last render's viewport so pagination matches the
  // partial being extended.
  if (!inputActive && section && !section->isBuilding() && section->isPartial() && !RenderLock::peek() &&
      buildViewportWidth > 0 && !partialRebuildStartFailed &&
      section->currentPage + PARTIAL_REBUILD_START_MARGIN >= static_cast<int>(section->pageCount)) {
    RenderLock lock;
    HalPowerManager::Lock fullClock;
    // Reuse the last render's viewport so the extension paginates identically to the partial.
    const ReaderRenderSpec buildSpec = SETTINGS.readerRenderSpec(buildViewportWidth, buildViewportHeight, prefs_);
    if (!section->startBuild(buildSpec)) {
      // Not fatal: the partial keeps serving its pages; crossing the watermark falls back to
      // the blocking extension in render(). Don't retry every tick.
      partialRebuildStartFailed = true;
      LOG_ERR("ERS", "Failed to start deferred partial extension build");
    } else {
      LOG_DBG("ERS", "Reader near partial watermark (%d/%d), resuming extension build", section->currentPage,
              section->pageCount);
    }
  }

  // Drive any in-progress incremental section build forward, off the page-turn critical path,
  // but only within a small window ahead of the reader: an unbounded build monopolized the
  // RenderLock and locked out page turns. The build follows the reader instead, and instant
  // reopen comes from suspendBuild() persisting the laid-out pages as a partial on exit.
  // Skip while the render mutex is busy so we never delay a pending render; re-check
  // isBuilding() under the lock since render() may have just finished it.
  // While extending a partial (rebuild from a previous session), pageCount is pinned at the
  // partial's watermark until the build catches up, so the window check would wrongly read
  // "far enough ahead" and stall the build at 0 pages -- then the first turn past the
  // watermark re-parses the whole chapter synchronously. Keep ticking until it finalizes.
  if (!inputActive && !RenderLock::peek() && backgroundBuildWanted() && buildTickHeapGate()) {
    RenderLock lock;
    // Re-check under the lock: render() (which also holds the RenderLock) may have finalized the
    // build between the outer isBuilding() check and acquiring the lock here, in which case
    // buildSomeMore() would fail and wrongly reset the section. The heap gate must be re-read
    // too: a render that won the lock race can expand retained glyph buffers, invalidating the
    // pre-lock heap reading. cppcheck can't see the cross-task mutation, so it flags this as
    // always true.
    // cppcheck-suppress knownConditionTrueFalse
    if (section->isBuilding() && buildTickHeapGate()) {
      const int pageCountBefore = static_cast<int>(section->pageCount);
      const int waitingPage = section->currentPage;
      const uint32_t loansBefore = renderer.frameBufferLoanCount();
      const bool built = section->buildSomeMore(BACKGROUND_BUILD_PAGES_PER_TICK);
      // An image probe may have lent the framebuffer, handing it back white while the panel still
      // shows the page. Redraw it so nothing is later painted over the blank buffer.
      if (renderer.frameBufferLoanCount() != loansBefore) requestUpdate();
      if (!built) {
        LOG_ERR("ERS", "Background section build failed");
        section.reset();
        requestUpdate();
      } else if (section->isBuildComplete() && applyDeferredReposition()) {
        // The chapter re-paginated since the saved progress (settings changed): we now know the
        // real page count, so re-render at the remapped page. No-op for an unchanged resume.
        requestUpdate();
      } else if (waitingPageBecameReadable(waitingPage, pageCountBefore, static_cast<int>(section->pageCount))) {
        requestUpdate();
      }
    }
  }

  if (!inputActive && lastRenderCompleteMs != 0 && millis() - lastRenderCompleteMs > IDLE_PREWARM_DEBOUNCE_MS)
    prefetchNextChapter();
}

// Lays out the next chapter a couple of pages per idle tick once the reader nears this
// one's end, so the turn into it loads a finished cache instead of building on the spot.
void EpubReaderActivity::prefetchNextChapter() {
  // The render task resets nextSection when it opens a chapter: every read is under the lock.
  RenderLock lock(RenderLock::TryOnly{});
  if (!lock.owns()) return;
  if (nextSection) {
    if (!buildTickHeapGate()) return;
    const uint32_t loansBefore = renderer.frameBufferLoanCount();
    const bool built = nextSection->buildSomeMore(BACKGROUND_BUILD_PAGES_PER_TICK);
    // An image probe may have lent the framebuffer, handing it back white under the shown page.
    if (renderer.frameBufferLoanCount() != loansBefore) requestUpdate();
    if (!built) {
      LOG_ERR("ERS", "Prefetch of chapter %d failed", prefetchedSpine);
      nextSection.reset();
    } else if (nextSection->isBuildComplete()) {
      LOG_INF("ERS", "Next chapter %d ready: %u pages, built", prefetchedSpine, nextSection->pageCount);
      nextSection.reset();
    }
    return;
  }
  const int next = currentSpineIndex + 1;
  if (sortesMode || !section || section->isBuilding() || section->isPartial() || buildViewportWidth == 0 ||
      next >= epub->getSpineItemsCount() || prefetchedSpine == next ||
      section->currentPage + PREFETCH_START_MARGIN < static_cast<int>(section->pageCount) || !buildTickHeapGate())
    return;
  prefetchedSpine = next;  // one attempt per chapter, whatever happens below
  if (epub->getCumulativeSpineItemSize(next) - epub->getCumulativeSpineItemSize(currentSpineIndex) >
      BUILD_POPUP_BYTE_THRESHOLD)
    return;
  HalPowerManager::Lock fullClock;
  nextSection = makeUniqueNoThrow<Section>(*epub, next, renderer);
  if (!nextSection) return;
  // Its own margins: a title that wraps to more lines makes the status bar band taller.
  int top, right, bottom, left;
  pageMargins(top, right, bottom, left, next);
  const ReaderRenderSpec spec = SETTINGS.readerRenderSpec(renderer.getScreenWidth() - left - right,
                                                          renderer.getScreenHeight() - top - bottom, prefs_);
  if (nextSection->loadSectionFile(spec) && !nextSection->isPartial()) {
    LOG_INF("ERS", "Next chapter %d ready: %u pages, cached", next, nextSection->pageCount);
    nextSection.reset();
  } else if ((!nextSection->hasHtmlCache() && ESP.getMaxAllocHeap() < PREFETCH_MIN_MAX_ALLOC) ||
             !nextSection->startBuild(spec)) {
    LOG_INF("ERS", "Prefetch of chapter %d skipped", next);
    nextSection.reset();
  }
}
