#pragma once
#include "EpubReaderMenuActivity.h"
#include "EpubReaderState.h"
#include "ProgressMapper.h"
#include "ReaderLanding.h"
#include "activities/Activity.h"

class Page;  // for drawParagraphNumbers (full type in the .cpp via <Epub/Page.h>)

class EpubReaderActivity final : public Activity, private EpubReaderState {
  bool blockSortesAction();
  void buildChapterMarks();
  const char* printedPageLabel() const;
  void awaitParagraphInBuild(uint16_t ordinal);
  // Call under RenderLock, before changing the reading position.
  void recordJumpOrigin(bool accepted = true);
  void jumpToContentOffset(int spineIndex, uint32_t offset, int pageHint = 0);
  void rememberBookOnceRendered();
  // The back-stack outlives the reader (sleep, home, sync) in links.bin so Back
  // still returns to where a followed link was tapped.
  void saveLinkStack() const;
  void loadLinkStack();

  // Returns nullptr when the allocation fails, exactly as the old per-page allocation did.
  uint8_t* acquireGrayscaleStripScratch(size_t bytes);
  void releaseGrayscaleStripScratch();

  // render() phases.
  void showPendingSyncSaveError();
  // A build failure leaves the "Indexing" popup up with no way forward; this replaces it
  // with the error, its code and the heap at the moment of failure.
  void showBuildError(Section::BuildFailure code, uint32_t freeHeap, uint32_t maxAlloc);
  void failBuild(const char* what, GfxRenderer::FrameBufferLoan* loan = nullptr);
  bool buildToCurrentPage();
  bool openSectionForRender(const ReaderRenderSpec& renderSpec);
  // loop() phase: prewarm, progress save and background builds while input is idle.
  void runIdleBackgroundWork();
  void renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
  void renderStatusBar() const;
  // Gate for a background build tick: true when the heap can take parse allocations.
  // Updates buildHeapPaused as a side effect.
  bool buildTickHeapGate();
  bool backgroundBuildWanted() const {
    return section && section->isBuilding() &&
           (section->isPartial() || static_cast<int>(section->pageCount) < section->currentPage + BUILD_WINDOW_AHEAD);
  }
  // Draw the indexing popup mid-build (parser image-probe callback and deadline backstop).
  void showBuildPopup();
  // Map the cached content position into the rebuilt section (used after a
  // settings change re-paginates a chapter). Returns true if currentPage moved.
  // No-op while the section is still building or when the pagination is unchanged (plain resume).
  bool applyDeferredReposition();
  // The saved resume/reflow anchor is only valid until it has established the
  // initial landing page. Later user navigation must never be overwritten when
  // a background section build finishes.
  void clearDeferredReposition();
  // Which landing anchors are pending. reader_landing decides which of them wins; this
  // only reports what is set. See ReaderLanding.h.
  reader_landing::Pending landingPending() const;
  void rememberCurrentContentOffset();
  bool saveProgress(int spineIndex, int currentPage, int pageCount);
  // Ordinary renders, including changed pagination, wait until the batch is due.
  void queueProgressSave(int spineIndex, int currentPage, int pageCount);
  // Write whatever the debouncer is still holding. Call before the book goes away.
  bool flushQueuedProgress();
  // Jump to a percentage of the book (0-100), mapping it to spine and page.
  void jumpToPercent(int percent);
  // Jump to a paragraph number as shown by the paragraph-number marks. Numbering restarts
  // at 1 in every chapter, so the number is always an ordinal within the current one.
  void jumpToParagraph(int target);
  // First page of a loaded section whose lines reach the given per-chapter ordinal
  // (returns the last page when the ordinal is past the chapter's paragraphs).
  int findPageForOrdinal(Section& section, uint16_t ordinal, bool* outFound = nullptr) const;
  // First paragraph ordinal appearing on `page`, or nullopt when the page holds no
  // numbered line (an image-only page, or a page not built yet).
  std::optional<uint16_t> firstOrdinalOnPage(Section& section, int page) const;
  // Remember where the reader is, as a paragraph ordinal, before dropping the section
  // for a re-layout. No-op when the ordinal cannot be read.
  void captureOrdinalAnchor();
  // Anchor the position and drop the section so the next render lays it out again.
  // The caller MUST already hold the render lock; reloadForReaderPrefsChange() is the
  // entry point for callers that do not.
  void dropSectionForRelayout();
  // Keep the reading position (content offset, page, chapter total) in the cached_*
  // fields the rebuild restores from, before the section is dropped. Caller holds the
  // render lock; no-op without a section.
  void cacheSectionPosition();
  void onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action);
  // Reader-menu actions, one per row; onReaderMenuConfirm routes to them.
  // Navigation
  void openChapterSelection();
  void openFootnotes();
  void openPercentSelection();
  void openPageEntry();
  // Whole-book reading percent (util/ReadingPercent.h) for every place that shows one.
  int cachedTotalPages() const;
  int bookPercent() const;
  void openParagraphEntry();
  void openBookmarks();
  void openSearch();
  // A bookmark picked in the list: jump to it by content offset, else by saved page.
  void onBookmarkJumpResult(const ActivityResult& result);
  // Reading tools
  void openDictionaryHistory();
  void openQuotesViewer();
  void openPageQr();
  // Lock-screen wallpaper
  void toggleWallpaperFavorite();
  void toggleWallpaperHold();
  void toggleWallpaperPause();
  void confirmWallpaperDelete();
  // This book's look
  void openStealLook();
  void openReadingThemes();
  void openReaderSettings();
  void openStatusBarSettings();
  void confirmResetReaderSettings();
  // The book file; each leaves the reader. onReaderMenuConfirm refuses them in Sortes mode.
  void removeFromRecents();
  void confirmDeleteBook();
  void deleteCacheAndGoHome();
  // Opens the reader menu for the current position (short-press Confirm)
  void openReaderMenu();
  // Reading margins plus status bar bands: where render() puts the page.
  void pageMargins(int& top, int& right, int& bottom, int& left);
  void openDictionaryWordSelect(int anchorX = -1, int anchorY = -1);
  // Opens the Grab Quote word-range picker on the current page.
  void openQuoteGrab();
  // Runs one of the CrossPointSettings::LONG_PRESS_MENU_FUNCTION actions. Shared by all
  // three bindings (long-press Confirm, hold inside the menu, double-click power) so each
  // one offers exactly the same list. Returns true when the function actually ran.
  bool runBoundMenuFunction(uint8_t function);
  // True when `function` could run right now: the page has a footnote, paragraph numbering
  // is on, KOReader credentials exist, and so on. The Quick Menu marks the rest [X] rather
  // than hiding them, so rows never move under the user's thumb.
  bool boundMenuFunctionAvailable(uint8_t function) const;
  // Opens the Quick Menu over the current page, listing the actions ticked into
  // SETTINGS.popupItems. No-op when nothing is ticked.
  void openQuickMenu();
  // Returns true if sync acted (launched, or surfaced a save error); false if it was a no-op
  // because no KOReader credentials are stored.
  // autoMode: Auto Sync's pull on open (saved networks only, no prompts).
  bool launchKOReaderSync(bool autoMode = false);
  // Handoff to a sync/transfer screen that reopens the book on the way out. The first
  // saves the position that reopen lands on (false: error surfaced, abort the handoff);
  // the second frees the Epub and Section for the radio. Neither holds the render lock
  // on entry; the release takes it.
  bool saveProgressForHandoff(const char* tag, int currentPage, int totalPages);
  void releaseBookForHandoff();
  // Trades this book's position with another reader over ESP-NOW, no network involved.
  void launchNearbyPositionSync();
  // Sends this book's own file to another reader over ESP-NOW.
  void launchNearbyBookSend();
  void applyOrientation(uint8_t orientation);
  // Per-book reader settings (#9).
  std::string readerOverridePath() const;
  void loadReaderPrefs();
  bool writeReaderOverride(const ReaderPrefs& p) const;
  // Capture the in-book Reader Settings edit back into this book's override.
  void applyReaderSettingsEdit();
  // Called on every row change inside the Reader Settings screen, via the screen's
  // sink, so the book's sidecar is already correct if the reader is switched off in
  // there. Writes only: prefs_ still holds what the page was laid out with, so the
  // re-layout decision at applyReaderSettingsEdit() is unaffected.
  void persistReaderSettingsEdit(const ReaderPrefs& live) const;
  static void readerEditSinkThunk(void* ctx, const ReaderPrefs& live);
  // The light panel's aux row is this book's text size. Only the sizes actually installed
  // for the active family are reachable, so on a device carrying only the built-in
  // ChareInk (one compiled-in size) the row shows the size and the steppers do nothing.
  bool lightPanelAuxText(char* out, size_t length) const override;
  bool lightPanelStepAux(int delta) override;

  static void statusBarEditSinkThunk(void* ctx, const StatusBarBlock& edited);
  void persistStatusBarEdit(const StatusBarBlock& edited);
  // Makes `bar` the global layout, keeping global's show/hide switch and hidden-bar progress bar.
  void setGlobalBarLayout(const StatusBarBlock& bar);
  // Reads the Customise Status Bar screen's result back into this book's override.
  void applyStatusBarEdit();
  // Delete this book's override and follow the global settings again.
  void resetReaderPrefsToGlobal();
  // Drop the section so the next render re-paginates with the new prefs, keeping position.
  void reloadForReaderPrefsChange();
  // Copies the reader settings stored in another book's cache dir onto this book.
  void applyStolenLook(const std::string& sourceCachePath);
  // Adopt reader settings from another book or a saved preset. Returns what was really
  // adopted — an SD font that is no longer installed is swapped for the built-in family.
  ReaderPrefs applyReaderPrefsFrom(const ReaderPrefs& incoming);
  // Paragraph numbers (#10).
  void applyParagraphNumbering(uint8_t mode, uint8_t size);
  // Paperback Look: per-book heavier-ink toggles (body text + status bar).
  void applyPaperbackLook(uint8_t body, uint8_t status);
  void applyStatusBar(uint8_t enabled, uint8_t progressBar);
  void drawParagraphNumbers(const Page& page, int marginLeft, int marginTop, int fontId);
  void loadQuoteAnchors();
  void drawQuoteUnderlines(const Page& page, int marginLeft, int marginTop, int fontId);
  void pageTurn(bool isForwardTurn);
  // Leave the End-of-Book screen for the book's last page.
  void returnToLastPage();
  void loadCachedBookmarks();
  void addBookmark();

  // Footnote navigation
  void navigateToHref(const std::string& href, bool savePosition = false);
  void restoreSavedPosition();

 public:
  explicit EpubReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Epub> epub,
                              int initialRefreshCountdown, bool sortesMode = false)
      : Activity(activity_name::kEpubReader, renderer, mappedInput),
        EpubReaderState(std::move(epub), initialRefreshCountdown, sortesMode) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  // Full CPU speed + fast loop ticks while a section build has work to do: at the low-power
  // frequency a giant chapter's background rebuild stretches from ~40s to many
  // minutes, so the reader exits before it can finalize and the next open restarts
  // it from page 0. Reverts to normal power behavior the moment the build finishes,
  // while it waits for the reader to come within the build window, and while it is
  // heap-paused (no work is happening, so spinning at full speed would only burn
  // battery; the paused gate still retries every loop pass).
  bool skipLoopDelay() override { return backgroundBuildWanted() && !buildHeapPaused; }
  bool isReaderActivity() const override { return true; }
  bool isBookContext() const override { return true; }
  bool applyReaderOrientation(const uint8_t orientation) override {
    applyOrientation(orientation);
    return true;
  }
  // The bindings router hands every gesture here first; the reader answers with the same
  // entry points the pop-up and the long-press bindings already use.
  bool runBoundAction(const uint8_t function) override { return runBoundMenuFunction(function); }
  bool appliesNightMode() const override { return true; }
  void onRevealed() override { scheduleGhostCleanup(); }
  bool handleForcedRefresh() override {
    {
      RenderLock lock(*this);
      pagesUntilFullRefresh = 0;
      forcedRefreshPending = true;
    }
    requestUpdate();
    return true;
  }
  ScreenshotInfo getScreenshotInfo() const override;
  CrossPointPosition getCurrentPosition() const;

  /**
   * The paragraph a sync should point at: the last one with text on the page
   * being read.
   *
   * The section's page table records the paragraph that was still being laid out
   * when each page filled up, so the entry for the current page names the
   * paragraph the reader can see at the bottom of it. Returns nothing when there
   * is no section loaded or no paragraph table for it.
   */
  std::optional<uint16_t> visibleParagraphIndex() const;
};
