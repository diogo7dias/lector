#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_random.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "DictionaryHistoryActivity.h"
#include "DictionaryWordSelectActivity.h"
#include "EpubReaderActivity.h"
#include "EpubReaderSearchActivity.h"
#include "MappedInputManager.h"
#include "QuoteSelectActivity.h"
#include "ReaderPresetStore.h"
#include "SdCardFontSystem.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "util/SortesSelection.h"

// Opens the section for currentSpineIndex: loads its cache or builds it (blocking full
// build, or the first chunk of an incremental one). False when render() must return now:
// an error is on screen, a Sortes skip moved on, or a relayout was requested.
bool EpubReaderActivity::openSectionForRender(const ReaderRenderSpec& renderSpec) {
  const auto filepath = epub->getSpineItem(currentSpineIndex).href;
  LOG_DBG("ERS", "Loading file: %s, index: %d", filepath.c_str(), currentSpineIndex);
  section = makeUniqueNoThrow<Section>(*epub, currentSpineIndex, renderer);
  printedMarksStale_ = true;  // a new layout puts the printed pages elsewhere
  if (!section) {
    LOG_ERR("ERS", "OOM: Section");
    returnHistory.finishReturn(false);
    return false;
  }
  // Fresh section, fresh chance: a failed lazy extension start in a previous
  // section must not suppress watermark-triggered rebuilds for this one.
  partialRebuildStartFailed = false;

  // A finalized cache serves every page as-is. A partial cache (suspended build from a
  // previous session) serves its pages instantly too, but a build must still run to lay
  // out the rest -- it re-parses from the top in the background (HTML already cached,
  // pages are deterministic) and finalizes, so the partial machinery retires itself.
  const bool cacheLoaded = section->loadSectionFile(renderSpec);
  if (cacheLoaded) {
    // Matching render params means identical pagination, so the saved page number is valid
    // as-is: consume any pending settings-change reposition. Without this, a chapter total
    // saved while the section was still building (i.e. a watermark, not the real count)
    // would remap the resume page against the finalized count and teleport the reader.
    cachedChapterTotalPageCount = 0;
    cachedVisibleTextOffset.reset();
  }
  const bool cacheComplete = cacheLoaded && !section->isPartial();
  // WHICH anchor governs this landing is named in ReaderLanding.h; the lookup it implies
  // stays here, where the section is. Read after the cache-hit reset above: a spec match
  // means the saved page number still names the same content, so there is nothing to
  // reposition.
  const reader_landing::Pending pendingLanding = landingPending();
  const reader_landing::Anchor landingAnchor = reader_landing::forOffsetLanding(pendingLanding);
  const std::optional<uint32_t> offsetJump = landingAnchor == reader_landing::Anchor::ExplicitOffset ? pendingOffsetJump
                                             : landingAnchor == reader_landing::Anchor::ResumeOffset
                                                 ? cachedVisibleTextOffset
                                                 : std::nullopt;
  if (!cacheComplete) {
    if (section->isPartial()) {
      LOG_DBG("ERS", "Partial cache found (%d pages), resuming build...", section->pageCount);
    } else {
      LOG_DBG("ERS", "Cache not found, building...");
    }

    // Jumps that need the final pagination or the anchor map -- explicit page jumps,
    // fragment anchors, percent jumps, and cross-setting progress repositioning -- can't
    // resolve their landing page until the whole chapter is laid out, so they take the full
    // (blocking) build with the indexing popup. Everything else -- plain forward reads, resume,
    // and explicit page jumps -- only needs a specific page, so it builds incrementally to that
    // page and finishes the rest in loop(). The settings-change reposition (cachedChapterTotal*)
    // is NOT a full-build trigger: it's deferred to applyDeferredReposition() once the real page
    // count is known, so it never blocks the first page.
    // Only a percent jump truly needs the whole chapter up front (percent -> page needs the final
    // page count). Anchor jumps (TOC / chapter select / footnotes) resolve incrementally below --
    // the anchor is recorded as its page is laid out, so a chapter-top anchor lands on page 0
    // without indexing the whole chapter.
    const bool needsFullBuild = reader_landing::needsFullBuild(pendingLanding);
    if (needsFullBuild) {
      GUI.drawPopup(renderer, tr(STR_INDEXING));
      scheduleGhostCleanup();
      // The popup's own refresh is a plain FAST, so force the page that replaces it onto the HALF
      // ghost-cleanup path -- otherwise the "INDEXING" text ghosts under the rendered page.
      // No popup redraws while the framebuffer is lent to the build below;
      // the panel holds the popup displayed above (e-ink is persistent).
      const PopupFn popupFn = [](void* const ctx) {
        auto* const self = static_cast<EpubReaderActivity*>(ctx);
        if (self->renderer.hasFrameBuffer()) GUI.drawPopup(self->renderer, tr(STR_INDEXING));
        self->scheduleGhostCleanup();
      };
      // Lend the framebuffer's 48 KB to the blocking full build; restored
      // (white) at scope exit, and the page render below redraws everything.
      // The strip scratch is dead weight during a build and competes with the arena it needs.
      releaseGrayscaleStripScratch();
      GfxRenderer::FrameBufferLoan loan(renderer);
      if (!section->createSectionFile(renderSpec, popupFn, this)) {
        failBuild("Failed to persist page data to SD", &loan);
        return false;
      }
      loan.end();
    } else {
      // Lay out just enough to show the landing page; loop() builds the rest behind it. Show the
      // indexing popup up front only when the build will actually be slow: a large spine (its
      // whole HTML must be inflated before page 1 can lay out -- the giant single-spine case), or
      // a deep resume/jump that must lay out many pages to reach the landing page. Tiny sections
      // build in a blink and stay popup-free.
      const int target = pendingPageJump.has_value() ? *pendingPageJump : (nextPageNumber < 0 ? 0 : nextPageNumber);
      // What the build must reach is named in ReaderLanding.h: the fragment anchor, else the
      // offsetJump above, else `target`.
      const bool anchorJump =
          reader_landing::forIncrementalBuild(pendingLanding) == reader_landing::Anchor::FragmentAnchor;

      // Landing well inside a partial: the page (or anchor, via the on-disk map) is already
      // servable, so don't restart the extension build now -- it re-lays out the WHOLE chapter
      // from page 0 (minutes of background CPU + SD writes on a giant spine), pure waste when
      // the reader never nears the watermark this session. loop() starts it lazily once the
      // reader is within PARTIAL_REBUILD_START_MARGIN pages of the watermark.
      if (section->isPartial() &&
          (anchorJump   ? section->getPageForAnchor(pendingAnchor).has_value()
           : offsetJump ? section->getPageForVisibleTextOffset(*offsetJump).has_value()
                        : target + PARTIAL_REBUILD_START_MARGIN < static_cast<int>(section->pageCount))) {
        LOG_DBG("ERS", "Partial covers target %d of %d; deferring extension build", target, section->pageCount);
      } else {
        const size_t spineBytes = epub->getCumulativeSpineItemSize(currentSpineIndex) -
                                  (currentSpineIndex > 0 ? epub->getCumulativeSpineItemSize(currentSpineIndex - 1) : 0);
        // Popup only when the build will actually be slow: a big spine whose HTML still needs
        // inflating (the multi-second cost), or a deep page target. A reopen with cached HTML builds
        // fast, so no popup -- that's what made an already-indexed book look like it was reindexing.
        // A partial cache that already covers the target page shows it instantly: never popup.
        const bool willInflate = !section->hasHtmlCache();
        bool showPopup;
        if (anchorJump) {
          // An anchor jump's cost is bounded by the anchor's page, not `target`. An anchor already
          // in the on-disk map (partial or finalized cache) lands instantly: no popup. Otherwise it
          // lies beyond the indexed watermark and the build may lay out the whole spine to find it,
          // so gate on spine size alone -- laying out a big spine takes seconds even with cached
          // HTML. Ordinary chapter-top TOC jumps resolve on page 0 and stay popup-free.
          showPopup = !section->findAnchor(pendingAnchor).has_value() && spineBytes > BUILD_POPUP_BYTE_THRESHOLD;
        } else if (offsetJump.has_value()) {
          // A relayout after a settings change: a predicted popup costs a refresh before
          // the build even starts, and most rebuilds to the reader's place are quick. The
          // deadline backstop below still surfaces it if this one drags.
          showPopup = false;
        } else {
          const bool targetAvailable = target < static_cast<int>(section->pageCount);
          showPopup = !targetAvailable &&
                      ((spineBytes > BUILD_POPUP_BYTE_THRESHOLD && willInflate) || target > BUILD_POPUP_PAGE_THRESHOLD);
        }
        if (showPopup) {
          GUI.drawPopup(renderer, tr(STR_INDEXING));
          scheduleGhostCleanup();
          // HALF-clear the popup when the page replaces it, else "INDEXING" ghosts under the page.
        }
        // Mid-build popup surfacing for slow builds the predictive gates can't
        // see (image extraction/probing inside a single page, or any chunk
        // overrunning the deadline). The parser fires the callback before the
        // first image probe; buildPopupPending gates it to this blocking phase
        // so a background build in loop() can never draw over a displayed page.
        buildPopupPending = !showPopup && !quietRelayout_;
        quietRelayout_ = false;
        const unsigned long buildStartMs = millis();
        bool started;
        // Lend the framebuffer's 48 KB across the whole blocking build, not just
        // startBuild's inflation peak. Page layout allocates a per-page text arena,
        // and this loop is the lowest-headroom moment in the system: unlike the
        // background builder in loop() it has no heap gate, so an oversized chapter
        // could fail its arena allocation here and take the whole chapter down with
        // it. The loan is dropped around any popup so drawing still works.
        releaseGrayscaleStripScratch();
        auto loan = makeUniqueNoThrow<GfxRenderer::FrameBufferLoan>(renderer);
        {
          // The popup drops the framebuffer loan before drawing, so both the activity and
          // the loan travel to the callback as one context.
          struct PopupContext {
            EpubReaderActivity* self;
            std::unique_ptr<GfxRenderer::FrameBufferLoan>* loan;
          } popupContext{this, &loan};
          started = section->startBuild(
              renderSpec,
              [](void* const ctx) {
                auto* const popup = static_cast<PopupContext*>(ctx);
                popup->loan->reset();
                popup->self->showBuildPopup();
              },
              &popupContext);
        }
        if (!started) {
          loan.reset();  // restore before anything draws
          failBuild("Failed to start section build");
          return false;
        }
        while (!section->isBuildComplete() &&
               (anchorJump               ? !section->findAnchor(pendingAnchor)
                : offsetJump.has_value() ? !section->buildReachedVisibleTextOffset(*offsetJump)
                                         : static_cast<int>(section->pageCount) <= target)) {
          // Anchor jump: build until the anchor's page is laid out (usually page 0), checking a
          // partial's on-disk anchor map too so an already-indexed anchor resolves immediately.
          // Re-pagination: build until the content the reader was on has been laid out. Costs the
          // same parse work as the old page target did -- it is the same content -- but it stops
          // at the right place, so the landing page is known before anything is drawn.
          // Otherwise: build until the target page exists. loop() builds the rest behind it.
          if (buildPopupPending && millis() - buildStartMs >= BUILD_POPUP_DEADLINE_MS) {
            // The predictive gates guessed fast but the build blew the silent budget.
            loan.reset();
            showBuildPopup();
          }
          if (!section->buildSomeMore(BUILD_PAGES_PER_CHUNK)) {
            loan.reset();  // restore before anything draws
            failBuild("Failed during incremental section build");
            return false;
          }
        }
        loan.reset();  // hand the framebuffer back before the page is drawn
        buildPopupPending = false;
      }
    }
  } else {
    LOG_DBG("ERS", "Cache found, skipping build...");
  }

  if (pendingSortesPage) {
    if (section->pageCount == 0) {
      section.reset();
      if (++sortesEmptyChapters >= epub->getSpineItemsCount()) {
        onGoHome();
        return false;
      }
      currentSpineIndex = (currentSpineIndex + 1) % epub->getSpineItemsCount();
      requestUpdate();
      return false;
    }
    nextPageNumber = sortes::below(section->pageCount, esp_random);
    pendingSortesPage = false;
  }

  if (pendingPageJump.has_value()) {
    section->currentPage = *pendingPageJump;
    pendingPageJump.reset();
  } else {
    section->currentPage = nextPageNumber;
    if (section->currentPage < 0) {
      section->currentPage = 0;
    }
  }

  // The chapter re-paginated, so nextPageNumber above named the old pagination's page.
  // The build loop stopped once this offset was laid out, so resolve it now, before the
  // first draw. Leaving it to applyDeferredReposition() is what made the stale page paint
  // first and then jump when the background build finished the chapter.
  if (offsetJump.has_value()) {
    if (const auto offsetPage = section->getPageForVisibleTextOffset(*offsetJump)) {
      section->currentPage = *offsetPage;
      // This anchor has now established the landing page. It must not be applied again
      // by applyDeferredReposition() after a background build finishes, or it would
      // undo page turns made during that build.
      clearDeferredReposition();
    } else if (landingAnchor == reader_landing::Anchor::ExplicitOffset) {
      LOG_ERR("ERS", "Could not resolve content offset");
      pendingOffsetJump.reset();
      section.reset();
      showBuildError(Section::BuildFailure::None, 0, 0);
      return false;
    }
  }
  if (reader_landing::retiresDeferredReposition(pendingLanding)) {
    // A bookmark/Return target supersedes any stale session-start resume anchor.
    clearDeferredReposition();
  }

  if (pendingPrefsMigration_ && section->pageCount > 0) {
    // The chapter is now laid out under the book's old settings, so the paragraph the
    // reader is on can be read off it. reloadForReaderPrefsChange() captures that
    // anchor and drops the section; the rebuild below lands on the same paragraph.
    pendingPrefsMigration_ = false;
    ReaderPrefs upgraded = prefs_;
    // A pre-v10 sidecar has no status bar byte, so the struct default (on) stood in
    // while this chapter was laid out. Seed the book from the global setting now, with
    // the position already anchored, because the switch changes the viewport.
    if (prefsFromVersion_ < FIRST_VERSION_WITH_STATUS_BAR_SWITCH) upgraded.statusBarEnabled = SETTINGS.sbEnabled;
    if (std::memcmp(&upgraded, &prefs_, sizeof(ReaderPrefs)) != 0) {
      prefs_ = upgraded;
      writeReaderOverride(prefs_);
      // Already inside render(), which holds the render lock — see dropSectionForRelayout.
      dropSectionForRelayout();
      requestUpdate();
      return false;
    }
    // Already matching the new defaults: just re-stamp the sidecar at the new version
    // so this does not run again on every open.
    writeReaderOverride(prefs_);
  }

  if (pendingOrdinalAnchor_.has_value() && section->pageCount > 0) {
    // The chapter has just been laid out again under different settings. Put the
    // reader back on the paragraph it was reading, wherever that landed.
    bool found = false;
    const int ordinalPage = findPageForOrdinal(*section, *pendingOrdinalAnchor_, &found);
    if (found || section->isBuildComplete()) {
      section->currentPage = ordinalPage;
      nextPageNumber = ordinalPage;
      pendingOrdinalAnchor_.reset();
      // The paragraph decided the page. Retire both fallbacks so the background
      // build's completion cannot rescale it a second time and teleport the reader.
      cachedChapterTotalPageCount = 0;
      cachedVisibleTextOffset.reset();
    }
    // Not laid out that far yet: keep the anchor. applyDeferredReposition() resolves
    // it once the build finishes, rather than landing on the build watermark now.
  }

  if (!pendingAnchor.empty()) {
    // Resolve from the pages laid out so far and/or the on-disk map (finalized or partial).
    const auto page = section->findAnchor(pendingAnchor);
    if (page) {
      section->currentPage = *page;
      LOG_DBG("ERS", "Resolved anchor '%s' to page %d", pendingAnchor.c_str(), *page);
    } else {
      LOG_DBG("ERS", "Anchor '%s' not found in section %d", pendingAnchor.c_str(), currentSpineIndex);
    }
    pendingAnchor.clear();
  }

  if (linkLandingPending) {
    linkLandingPending = false;
    linkLanding = {currentSpineIndex, section->currentPage};
    linkLandingReady.store(true, std::memory_order_release);
  }

  if (pendingPercentJump && section->pageCount > 0) {
    // Apply the pending percent jump now that we know the new section's page count.
    section->currentPage = reader_landing::percentPage(pendingSpineProgress, section->pageCount);
    pendingPercentJump = false;
  }

  if (pendingParagraphScan_.has_value() && section->pageCount > 0) {
    // A cross-chapter Go-to-Paragraph landed in this freshly loaded section; scan it
    // for the target local ordinal now that its pages exist.
    bool found = false;
    section->currentPage = findPageForOrdinal(*section, *pendingParagraphScan_, &found);
    if (!found && !section->isBuildComplete()) awaitParagraphInBuild(*pendingParagraphScan_);
    pendingParagraphScan_.reset();
  }
  return true;
}
