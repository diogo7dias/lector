#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "DictionaryWordSelectActivity.h"
#include "EpubReaderActivity.h"
#include "EpubReaderSearchActivity.h"
#include "EpubReaderUtils.h"
#include "MappedInputManager.h"
#include "QuoteSelectActivity.h"
#include "ReaderMargins.h"
#include "ReaderUtils.h"
#include "SdCardFontSystem.h"
#include "WatermarkBuildBail.h"
#include "activities/network/NearbyFileTransferActivity.h"
#include "activities/settings/TextSettingsActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookProgressFile.h"
#include "util/ScreenshotUtil.h"
#include "util/SortesSelection.h"

// Where the page sits on screen: the reading margins plus the status bar bands. render()
// lays the page out inside these, and every overlay that hit-tests words on it (dictionary
// pick, quote grab) must use the same, or a hold lands a line off.
void EpubReaderActivity::pageMargins(int& top, int& right, int& bottom, int& left) {
  // This book's own bar: everything below measures with the same prefs it lays out with.
  const StatusBarBlock sb = statusBarOf(prefs_);

  ReaderUtils::readerMargins(renderer, prefs_, top, right, bottom, left);

  // Status bar (v2 per-item): reserve top and/or bottom bands. readerMargins
  // already folded the reading margins into top/bottom; the band is
  // ADDED on top of that (a real gap between the bar and the text), matching the
  // additive left/right margins. Changing the band shifts the viewport, which
  // re-paginates the section cache like any margin change.
  // A greedy (truncate-off) title can wrap to several lines; reserve the extra band
  // height in whichever edge holds the title so the reading text is pushed clear.
  // Stacked titles (book above chapter) need a line each, whatever Truncate Title says.
  int sbTitleExtraPx = 0;
  bool sbTitleTop = false;
  if (sb.textOn() && sb.titleSlot(true).anchor != CrossPointSettings::SB_ANCHOR_OFF) {
    std::string chapterTitle;
    const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
    if (tocIndex != -1) chapterTitle = epub->getTocItem(tocIndex).title;
    if (chapterTitle.empty()) chapterTitle = tr(STR_UNNAMED);
    const int lines = UITheme::getStatusBarV2TitleLines(sb, renderer, true, chapterTitle.c_str(),
                                                        epub->getTitle().c_str(), &sbTitleTop);
    sbTitleExtraPx = (lines - 1) * renderer.getLineHeight(UI_10_FONT_ID);
  }
  const int sbTop = UITheme::getInstance().getStatusBarV2TopHeight(sb, true, sbTitleTop ? sbTitleExtraPx : 0);
  const int sbBottom = UITheme::getInstance().getStatusBarV2BottomHeight(sb, true, sbTitleTop ? 0 : sbTitleExtraPx);
  top += sbTop;
  bottom += sbBottom;
}

void EpubReaderActivity::showPendingSyncSaveError() {
  if (!pendingSyncSaveError) return;
  pendingSyncSaveError = false;
  GUI.drawPopup(renderer, tr(STR_SAVE_PROGRESS_FAILED));
  scheduleGhostCleanup();
}

// A section build failure (e.g. an invalid/corrupt EPUB that fails XML parsing) leaves the
// "Indexing" popup on screen with no way forward. Surface an explicit error instead of hanging.
// clearScreen first so the error popup doesn't overlay the stale "Indexing" popup.
// The diagnostic tail (code + heap at the moment of failure) is appended because these
// builds fail on-device, where there is usually no serial monitor attached. Code 7 with
// a low free heap means an allocation failure; code 7 with plenty of heap means the SD
// write failed. See Section::BuildFailure.
void EpubReaderActivity::showBuildError(const Section::BuildFailure code, const uint32_t freeHeap,
                                        const uint32_t maxAlloc) {
  returnHistory.finishReturn(false);
  renderer.clearScreen();
  char msg[96];
  if (code == Section::BuildFailure::None) {
    snprintf(msg, sizeof(msg), "%s", tr(STR_INDEX_FAILED));
  } else {
    snprintf(msg, sizeof(msg), "%s (E%d h%uk a%uk)", tr(STR_INDEX_FAILED), static_cast<int>(code), freeHeap / 1024,
             maxAlloc / 1024);
  }
  // Back is the only way out of this screen, so say so (upstream #3724).
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawPopup(renderer, msg);
  scheduleGhostCleanup();
}

// A failed build: read the failure before the reset destroys it, drop the section, and
// show the error. A framebuffer loan still held is returned after the section is freed,
// so the 48 KB comes back into the room the section leaves, and before anything draws.
void EpubReaderActivity::failBuild(const char* what, GfxRenderer::FrameBufferLoan* loan) {
  LOG_ERR("ERS", "%s", what);
  const auto failure = section->lastFailure();
  const auto failHeap = section->lastFailureFreeHeap();
  const auto failAlloc = section->lastFailureMaxAlloc();
  section.reset();
  if (loan) loan->end();
  buildPopupPending = false;
  showBuildError(failure, failHeap, failAlloc);
}

// Lay out chunks until the page to show exists. False when render() must return now:
// the build failed (error shown) or yielded so loop() can take input.
bool EpubReaderActivity::buildToCurrentPage() {
  while (!section->isBuildComplete() && section->currentPage >= static_cast<int>(section->pageCount)) {
    if (!section->buildSomeMore(BUILD_PAGES_PER_CHUNK)) {
      failBuild("Failed during incremental section build");
      return false;
    }
    if (watermarkBuildShouldYield(section->currentPage, static_cast<int>(section->pageCount),
                                  section->isBuildComplete())) {
      return false;
    }
  }
  return true;
}

void EpubReaderActivity::render(RenderLock&& lock) {
  if (!epub) {
    return;
  }

  // The Quick Menu is an overlay, not a screen: it paints straight onto the page already
  // in the framebuffer and returns, so opening it costs one popup-sized refresh instead of
  // a full page repaint. Closing it calls requestUpdate(), which lands here again with the
  // pop-up inactive and redraws the page normally.
  // Only over the page it was opened on: a framebuffer loan since that page was drawn (a build
  // step's image probe) handed the buffer back white. Then the page is redrawn first and the
  // pop-up follows on the next pass.
  if (quickMenu.isActive()) {
    if (renderer.frameBufferLoanCount() == pageLoanCount) {
      quickMenu.processRender(renderer, mappedInput);
      return;
    }
    requestUpdate(/*immediate=*/true);
  }
  pageLoanCount = renderer.frameBufferLoanCount();  // everything below repaints the framebuffer

  // edge case handling for sub-zero spine index
  if (currentSpineIndex < 0) {
    currentSpineIndex = 0;
  }
  // based bounds of book, show end of book screen
  if (currentSpineIndex > epub->getSpineItemsCount()) {
    currentSpineIndex = epub->getSpineItemsCount();
  }

  if (currentSpineIndex == epub->getSpineItemsCount()) {
    // Sole load site: runs on the render task (serialized by RenderLock); the main
    // task only reads the suggestions once the loaded flag is published
    endOfBookOptions.loadOnce(epub->getPath());
    renderer.clearScreen();
    endOfBookOptions.render(renderer, mappedInput);
    renderer.displayBuffer(endOfBookPainted ? HalDisplay::FAST_REFRESH : HalDisplay::HALF_REFRESH);
    endOfBookPainted = true;
    showPendingSyncSaveError();
    return;
  }
  endOfBookPainted = false;

  // Apply screen viewable areas, reading margins and the status bar bands.
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  pageMargins(orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);

  const uint16_t viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  const uint16_t viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;
  // Capture for loop()'s lazy partial-extension start (must match this render's layout params).
  buildViewportWidth = viewportWidth;
  buildViewportHeight = viewportHeight;

  const ReaderRenderSpec renderSpec = SETTINGS.readerRenderSpec(viewportWidth, viewportHeight, prefs_);
  // Layout and paint below both measure through the renderer, so the switches are set once
  // here; the deferred extension build in loop() reuses it with the same prefs.
  renderer.setKerning(renderSpec.fontId, renderSpec.kerning);
  renderer.setLigatures(renderSpec.fontId, renderSpec.ligatures);
  renderer.setCharacterSpacing(renderSpec.fontId, renderSpec.characterSpacing);
  renderer.setTextContrast(prefs_.textContrast);

  if (!section && !openSectionForRender(renderSpec)) return;

  // Extend the build to the requested page if needed (for partials and in-progress builds).
  // This runs every render, so it covers both the first page and any forward turn that gets
  // ahead of the background builder; pages already built do no work here.
  //
  // Crossing a partial's watermark before the extension rebuild has caught up means a
  // synchronous wait spanning the remaining prefix re-layout -- potentially tens of
  // seconds on a giant spine. Show the indexing popup so it isn't a silent freeze
  // (the page that replaces it takes the HALF ghost-cleanup path). Ordinary window
  // catch-ups on a non-partial build are a page or two and stay popup-free.
  if (section->isPartial() && section->currentPage >= static_cast<int>(section->pageCount)) {
    GUI.drawPopup(renderer, tr(STR_INDEXING));
    scheduleGhostCleanup();
  }
  while (section->isPartial() && section->currentPage >= static_cast<int>(section->pageCount)) {
    // Start a build to extend a partial toward the requested page.
    if (!section->isBuilding() && !section->startBuild(renderSpec)) {
      failBuild("Failed to start partial extension build");
      return;
    }
    // Extend one chunk, then yield if the requested page still does not exist so
    // loop() can take input. Instant reopen is suspendBuild() on exit, same as
    // the background builder.
    if (!buildToCurrentPage()) return;
  }
  // For an in-progress incremental build, make sure the page we're about to show has been laid out.
  if (section->isBuilding() && !buildToCurrentPage()) return;

  // The requested page is now as built as it will get. If it still lands past the end,
  // clamp to the last real page: the UINT16_MAX "last page" sentinel from backward chapter
  // navigation, an explicit jump beyond a finished chapter, or a stale saved position.
  // Guarded on !isBuilding() because a still-building section's pageCount is only the current
  // watermark (not the final count) and has already been driven far enough by the loops above.
  if (!section->isBuilding() && section->pageCount > 0 &&
      section->currentPage >= static_cast<int>(section->pageCount)) {
    section->currentPage = section->pageCount - 1;
  }

  // Apply a deferred settings-change reposition now that the real page count is known (a no-op for
  // a plain resume / unchanged pagination). If still building, this defers to loop() on completion.
  applyDeferredReposition();

  renderer.clearScreen();

  if (section->pageCount == 0) {
    if (!section->isBuilding()) {
      // An empty chapter skipped the clamp above, so a backward turn's last-page sentinel
      // (65535) stayed in currentPage and Back took one press per page to leave. Jumps
      // meant for a laid-out chapter must not fire in whatever chapter loads next either.
      section->currentPage = 0;
      pendingPercentJump = false;
      pendingParagraphScan_.reset();
    }
    returnHistory.finishReturn(false);
    LOG_DBG("ERS", "No pages to render");
    renderer.drawCenteredText(UI_10_FONT_ID, 300, tr(STR_EMPTY_CHAPTER), true, EpdFontFamily::REGULAR);
    renderStatusBar();
    renderer.displayBuffer();
    showPendingSyncSaveError();
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    returnHistory.finishReturn(false);
    LOG_DBG("ERS", "Page out of bounds: %d (max %d)", section->currentPage, section->pageCount);
    renderer.drawCenteredText(UI_10_FONT_ID, 300, tr(STR_OUT_OF_BOUNDS), true, EpdFontFamily::REGULAR);
    renderStatusBar();
    renderer.displayBuffer();
    showPendingSyncSaveError();
    return;
  }

  {
    // Unified page read: the in-progress build's in-RAM table if it has reached the page,
    // otherwise the on-disk file (finalized section, or a partial from a previous session).
    auto p = section->loadPage(section->currentPage);
    if (!p) {
      LOG_ERR("ERS", "Failed to load page from SD - clearing section cache");
      // Retrying rebuilds a transiently corrupt section and usually recovers, but a page that keeps
      // failing would loop forever on a blank screen, so bound the retries before giving up.
      const bool giveUp = ++pageLoadRetryCount > MAX_PAGE_LOAD_RETRIES;
      // Abandon (not suspend) any active build BEFORE clearing: clearCache deletes the files,
      // and the destructor's suspend would otherwise commit tables into a deleted handle.
      nextPageNumber = section->currentPage;  // retry the landing page, not a stale page hint
      section->abandonBuild();
      section->clearCache();
      section.reset();
      if (giveUp) {
        returnHistory.finishReturn(false);
        LOG_ERR("ERS", "Page load retry limit reached, aborting");
        pageLoadRetryCount = 0;  // Reset so a later user-initiated navigation can try afresh
        renderer.clearScreen();
        renderer.drawCenteredText(UI_10_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::REGULAR);
        renderer.displayBuffer();
        showPendingSyncSaveError();
        return;
      }
      requestUpdate();  // Try again after clearing cache
      showPendingSyncSaveError();
      return;
    }
    pageLoadRetryCount = 0;  // Reset the retry counter once a page loads cleanly

    // Cache this page's content offset (read alongside the page, no extra file open) so
    // saveProgress and addBookmark can use it without reopening section.bin.
    currentPageVisibleOffset = p->visibleTextOffset;
    currentPageSpineIndex = currentSpineIndex;
    pendingOffsetJump.reset();
    returnHistory.finishReturn(true);

    // Collect footnotes from the loaded page
    currentPageFootnotes = std::move(p->footnotes);

    // Refresh at Chapter Start: the first page of a chapter just entered gets the clean pass.
    if (SETTINGS.chapterStartRefresh && section->currentPage == 0 && currentSpineIndex != lastRenderedSpine) {
      scheduleGhostCleanup();
    }
    lastRenderedSpine = currentSpineIndex;

    const auto start = millis();
    renderContents(std::move(p), orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    LOG_DBG("ERS", "Rendered page in %dms", millis() - start);
    lastRenderCompleteMs = millis();
    pageRendered.store(true, std::memory_order_release);
    pageLoanCount = renderer.frameBufferLoanCount();  // after any loan taken by this render's build
  }
  // Observe every render so unchanged pages can still reach the time limit.
  queueProgressSave(currentSpineIndex, section->currentPage, section->estimatedTotalPages());

  showPendingSyncSaveError();

  if (pendingScreenshot) {
    pendingScreenshot = false;
    ScreenshotUtil::takeScreenshot(renderer);
  }

  if (showBookmarkMessage) {
    GUI.drawPopup(renderer, bookmarkRemoved ? tr(STR_BOOKMARK_REMOVED) : tr(STR_BOOKMARK_ADDED));
  }

  if (showDictionaryMessage) {
    GUI.drawPopup(renderer, tr(STR_DICT_NO_DICT_SET));
  }
}
