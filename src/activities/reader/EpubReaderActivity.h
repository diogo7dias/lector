#pragma once
#include <Epub.h>
#include <Epub/FootnoteEntry.h>
#include <Epub/Section.h>

#include <optional>
#include <vector>

#include "BookmarkEntry.h"
#include "EndOfBookOptions.h"
#include "EpubReaderMenuActivity.h"
#include "ProgressMapper.h"
#include "ReaderPrefs.h"
#include "ReaderProgressSaveDebouncer.h"
#include "ReaderUtils.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "reading_stats/ReaderStatsSession.h"
#include "reading_stats/SdStatsFiles.h"

class Page;  // for drawParagraphNumbers (full type in the .cpp via <Epub/Page.h>)

class EpubReaderActivity final : public Activity {
  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  // Per-book reader "look" settings. Loaded from <cachePath>/reader_override.bin on
  // enter if present (prefsCustom_ = true), else a snapshot of the global settings.
  // The reader lays out exclusively through prefs_, so the global singleton is never
  // mutated for reading and a custom book stays decoupled from global changes.
  ReaderPrefs prefs_;
  // Reading statistics. The session owns both stores (this book's and the global
  // one) and is fed page/turn events below; statsTrackingActive latches the
  // setting at open so toggling it mid-book cannot half-track a session.
  reading_stats::SdStatsFiles statsFiles;
  reading_stats::ReaderStatsSession statsSession{statsFiles};
  bool statsTrackingActive = false;
  bool prefsCustom_ = false;
  // Paragraph numbers (#10): per-spine visible-paragraph counts for whole-book
  // numbering, captured as pages render and persisted to paragraph_counts.bin so
  // the whole-book base survives reopen. Finalizes as the book is read through.
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  std::optional<uint16_t> pendingPageJump;
  // Go to Paragraph: after a cross-chapter jump, the target local paragraph ordinal to
  // scan for once the new section has loaded (consumed in the render path).
  std::optional<uint16_t> pendingParagraphScan_;
  // Paragraph ordinal the reader was showing when a layout-changing setting was
  // applied. A page number means nothing across a re-layout (a new font or indent
  // repaginates the chapter), but a paragraph is a property of the book and does not
  // move, so the position is carried across as an ordinal and resolved back to
  // whatever page now holds it. This is the same ordinal the in-book numbering draws
  // and Go To Paragraph accepts.
  std::optional<uint16_t> pendingOrdinalAnchor_;
  // True when this book's sidecar was written before the 0.8.1 reading defaults. The
  // upgrade is deferred until the chapter has been laid out under the old settings, so
  // the reading position can be carried across as a paragraph rather than a page.
  bool pendingPrefsMigration_ = false;
  // Which version the loaded sidecar was written at, so a migration only seeds fields
  // that version genuinely lacked.
  uint8_t prefsFromVersion_ = ReaderPrefs::VERSION;
  // Set when navigating to a footnote href with a fragment (e.g. #note1).
  // Cleared on the next render after the new section loads and resolves it to a page.
  std::string pendingAnchor;
  int pagesUntilFullRefresh = 0;
  // Any popup or banner painted over the page leaves charge the next fast differential
  // cannot clear, so the redraw that REMOVES it has to drive every pixel. Setting the
  // counter to 1 promotes that next paint to HALF via displayWithRefreshCycle, which is
  // the same idiom the image-page path already uses, and still honours
  // Refresh Frequency = Never. Call it where the overlay goes away, not where it is
  // drawn: a render that both draws and displays would consume the promotion itself.
  // 0, not 1. Both reach the HALF cleanup pass (displayWithRefreshCycle tests <= 1), but
  // the X3 no-flash reinforcement path added by upstream #2818 triggers on exactly 1. A
  // ghost is the one thing the gentle waveform must not be asked to clear, so these sites
  // say 0 and keep the strong pass. Upstream makes the same change at its own call sites.
  void scheduleGhostCleanup() { pagesUntilFullRefresh = 0; }
  // Image pages use a dedicated double-FAST refresh path, so retain a manual
  // refresh request until renderContents can issue its clean base pass.
  bool forcedRefreshPending = false;
  int cachedSpineIndex = 0;
  int cachedChapterTotalPageCount = 0;
  std::optional<uint32_t> cachedVisibleTextOffset;
  // Visible-codepoint offset of the page currently on screen, captured when the page is loaded
  // (Page::visibleTextOffset). Lets saveProgress persist the offset without reopening section.bin.
  std::optional<uint32_t> currentPageVisibleOffset;
  // Explicit "land at this visible-codepoint offset in the target spine" request (bookmark open).
  // Resolved in render() once the section is loaded/built far enough, then cleared. Unlike a
  // settings-change reposition it always resolves by content, so it survives any re-pagination.
  std::optional<uint32_t> pendingOffsetJump;
  // Signals that the next render should reposition within the newly loaded section
  // based on a cross-book percentage jump.
  bool pendingPercentJump = false;
  // Normalized 0.0-1.0 progress within the target spine item, computed from book percentage.
  float pendingSpineProgress = 0.0f;
  bool pendingScreenshot = false;
  bool pendingSyncSaveError = false;
  // Consecutive page-load failures. Each failure drops the section and rebuilds on the next render,
  // which recovers a transiently corrupt cache; capped so a persistently bad page can't spin forever.
  uint8_t pageLoadRetryCount = 0;
  static constexpr uint8_t MAX_PAGE_LOAD_RETRIES = 3;
  bool skipNextButtonCheck = false;  // Skip button processing for one frame after subactivity exit
  bool showBookmarkMessage = false;
  // "No dictionary set" popup, shown when a lookup is triggered without a configured dictionary.
  bool showDictionaryMessage = false;
  unsigned long dictionaryMessageTime = 0UL;
  bool ignoreNextConfirmRelease = false;
  bool currentPageBookmarked = false;
  // Idle-time glyph prewarm: after a page settles, scan the LIKELY next page
  // (scan mode draws nothing) and load its missing glyphs from SD during idle,
  // so the next turn's in-render prewarm is a cache hit instead of ~100 ms of
  // SD reads on the page-turn critical path. One attempt per position.
  int idlePrewarmSpine = -1;
  int idlePrewarmPage = -1;
  unsigned long lastRenderCompleteMs = 0;
  bool bookmarkRemoved = false;  // true when last toggle removed (controls popup text)
  std::vector<BookmarkEntry> cachedBookmarks;
  // Saved quotes keep a thin underline in the text. Only their POSITIONS live in
  // RAM (12 bytes each, capped); the quote text itself stays on the SD card and is
  // read back, bounded, on the rare page that might be showing it.
  struct QuoteAnchorRef {
    uint32_t textOffset;  // byte offset of the quote text inside the sidecar
    uint16_t textLength;
    uint16_t spine;
    uint16_t paragraph;  // chapter-local ordinal; 0 = unknown, search the chapter
    uint16_t wordHint;   // index the quote started at when grabbed; tie-break only
  };
  // Positions of this book's saved quotes, loaded once per open and refreshed
  // whenever the sidecar is rewritten (a grab, or a delete in the viewer).
  std::vector<QuoteAnchorRef> quoteAnchors;
  // Underline segments already worked out for the page on screen, so a repeat
  // render (status bar tick, return from a menu) costs no SD reads.
  struct UnderlineSegment {
    int16_t x1;
    int16_t x2;
    int16_t y;
  };
  std::vector<UnderlineSegment> underlineMemo;
  int underlineMemoSpine = -1;
  int underlineMemoPage = -1;
  // Tracks whether this book is currently removed from Recent Books by the
  // removeReadBooksFromRecents feature (set at End-of-Book, cleared if paged back in).
  bool recentsEntryRemoved = false;
  unsigned long bookmarkMessageTime = 0UL;
  // Set when the reader is left at end-of-book and SETTINGS.moveFinishedToReadFolder is on.
  // Consumed in onExit() to relocate the finished book into /Read/.
  bool pendingReadFolderMove = false;
  // Set by the reader menu's "Remove from Recents" row; acted on in onExit.
  bool pendingRemoveFromRecents = false;
  // Set once the Delete Book confirmation is accepted; the file and its cache are
  // erased in onExit, after the Epub (and its open handles) are released.
  bool pendingDeleteBook = false;
  // Next-book suggestion menu for the End-of-Book screen
  EndOfBookOptions endOfBookOptions;
  // Quick Menu: the pop-up a binding set to Menu Pop-up opens over the page. Owned by the
  // reader rather than being its own activity so the page underneath is never repainted —
  // the pop-up is drawn straight onto the framebuffer the page already occupies.
  OptionPopup quickMenu;
  // Parallel to quickMenu's rows: the binding value each row runs. The pop-up reports an
  // index, and the row order depends on which items are ticked, so the mapping cannot be
  // recomputed from the settings mask alone at callback time.
  std::vector<uint8_t> quickMenuFunctions;

  // Back and Confirm are acted on at RELEASE here, while child screens (the Settings
  // family) close on PRESS. These pair each release with the press this activity saw, so
  // a release left over by a closing child cannot be read as the user's own input.
  ReaderUtils::ButtonPressLatch backLatch_;
  ReaderUtils::ButtonPressLatch confirmLatch_;

  // Footnote support
  std::vector<FootnoteEntry> currentPageFootnotes;
  struct SavedPosition {
    int spineIndex;
    int pageNumber;
  };
  static constexpr int MAX_FOOTNOTE_DEPTH = 3;
  SavedPosition savedPositions[MAX_FOOTNOTE_DEPTH] = {};
  int footnoteDepth = 0;

  // Viewport of the last render(), captured so loop()'s lazy partial-extension start
  // builds with IDENTICAL layout parameters to the pages already rendered (a mismatch
  // would paginate differently than the partial being extended). 0 = no render yet.
  uint16_t buildViewportWidth = 0;
  uint16_t buildViewportHeight = 0;
  // Set when the lazy extension start failed, so loop() doesn't retry (and log) every
  // tick; the blocking extension in render() remains the fallback past the watermark.
  bool partialRebuildStartFailed = false;

  // Last position persisted by render()'s saveProgress, used to skip redundant
  // writeAtomic calls on no-op re-renders (menu/bookmark/screenshot).
  int lastSavedSpineIndex = -1;
  int lastSavedPage = -1;
  int lastSavedPageCount = -1;

  // Ordinary page turns are batched instead of writing progress on each one:
  // EpubReaderUtils::saveProgress() is a writeAtomic, several FAT operations for
  // six bytes, and on the X3 that lands between the button press and the page.
  // Flushed on exit, which is also the sleep path (ActivityManager::goToSleep()
  // replaces this activity and so runs onExit()).
  ReaderProgressSaveDebouncer progressSaveDebouncer;

  // Grayscale strip scratch for the blocking (X3) tier of renderContents(). It used to be
  // allocated and freed on every page render; a whole reading session of that churn measurably
  // decays the largest contiguous block. Kept alive between pages instead and handed back
  // before the heap-hungry work (section builds, leaving the book).
  std::unique_ptr<uint8_t[]> grayscaleStripScratch;
  size_t grayscaleStripScratchBytes = 0;
  // Returns nullptr when the allocation fails, exactly as the old per-page allocation did.
  uint8_t* acquireGrayscaleStripScratch(size_t bytes);
  void releaseGrayscaleStripScratch();

  void renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
  // Reader text margins from the per-book prefs: oriented viewable insets plus the
  // user screen margins (uniform or independent top/bottom, and dynamic horizontal
  // auto-widen toward ~62 chars/line). `bottom` is the base reading margin only; the
  // render path folds any status-bar band into the bottom separately (max-overlap).
  void computeReaderMargins(int& top, int& right, int& bottom, int& left) const;
  void renderStatusBar() const;
  // Pages laid out per incremental-build pump: on the render path (catching up to the page
  // being shown) and per loop() tick (background build of a large chapter). Kept small so a
  // background build chunk never noticeably delays input or a pending render.
  static constexpr int BUILD_PAGES_PER_CHUNK = 8;
  static constexpr int BACKGROUND_BUILD_PAGES_PER_TICK = 2;

  // MEMFIX-PORT: background-build heap floor; portable
  // Skip background build ticks below this free-heap floor. The parse path grows
  // word vectors of heap strings — throwing allocations that abort() on OOM under
  // -fno-exceptions (field crash: bad_alloc in ParsedText::addWord during a
  // background tick under heap pressure). The tick is deferrable work:
  // page-turn transients free up between turns and the build resumes; the render
  // path still builds the page it actually needs regardless of this floor.
  static constexpr size_t BACKGROUND_BUILD_MIN_FREE_HEAP = 32 * 1024;
  // Fragmentation floor for the same gate: a tick passed the free-heap floor at
  // 34.7 KB free but the largest block was ~11 KB, and a parse allocation inside the
  // tick aborted anyway. Free heap says how much memory exists; maxAlloc says whether
  // any single allocation can actually have it. 16 KB also keeps the advance-table
  // batch path (16 KB scratch) viable during builds.
  static constexpr size_t BACKGROUND_BUILD_MIN_MAX_ALLOC = 16 * 1024;
  // Gate for a background build tick: true when the heap can take parse allocations.
  // Updates buildHeapPaused as a side effect.
  bool buildTickHeapGate();
  // True while the background build is gated on the heap floors. Lets skipLoopDelay()
  // return the loop to normal delay/power-saving during the pause: isBuilding() stays
  // true the whole time, and without this the loop would spin at full CPU speed doing
  // no build work — indefinitely, if the build context itself keeps the heap low.
  bool buildHeapPaused = false;
  // Heap floor for optional render-adjacent work (idle prewarm). Page
  // deserialization (TextBlock word vectors/strings) and glyph caching allocate
  // through throwing paths that abort() on OOM; skip deferrable work below it.
  static constexpr size_t RENDER_MIN_FREE_HEAP = 24 * 1024;
  // How many pages to keep laid out ahead of the reader for a still-building section. A page
  // turn is ~1s on e-ink and a page builds in ~30ms, so the reader can't out-click the builder
  // -- a tiny buffer is enough. The background build stops once the watermark is this far
  // ahead and resumes as the reader advances; building unbounded instead locked up input by
  // monopolizing the RenderLock. A giant single-spine book therefore never finalizes its .bin
  // in one sitting -- instant reopen comes from Section::suspendBuild() persisting the pages
  // already laid out as a partial file on exit/sleep.
  static constexpr int BUILD_WINDOW_AHEAD = 5;
  // Reopening a partial does NOT immediately restart its extension build (a whole-chapter
  // re-layout from page 0 -- minutes of background CPU + SD writes on a giant spine, wasted
  // when the reader never crosses the watermark that session). Instead loop() starts it once
  // the reader is within this many pages of the watermark: at ~30s per page read and ~100-300ms
  // per page rebuilt, this margin gives the rebuild ample runway to catch up (and finalize)
  // before the reader arrives.
  static constexpr int PARTIAL_REBUILD_START_MARGIN = 15;
  // Show the indexing popup when an initial build must lay out more than this many pages up front
  // (a deep resume/jump into a not-yet-built section), so it isn't a silent wait. Kept independent
  // of the small look-ahead window so ordinary landings stay popup-free.
  static constexpr int BUILD_POPUP_PAGE_THRESHOLD = 20;
  // Also show the popup when first building a spine larger than this (uncompressed bytes): its
  // whole HTML must be inflated before page 1 can lay out (the giant single-spine case), which is
  // a multi-second wait. Normal chapters are well under this and stay popup-free.
  static constexpr size_t BUILD_POPUP_BYTE_THRESHOLD = 96 * 1024;
  // Deadline backstop for the predictive gates above: if the blocking build-to-target still
  // hasn't produced the landing page this long after the build started, surface the popup
  // mid-build. Builds that finish under the deadline stay popup-free.
  static constexpr unsigned long BUILD_POPUP_DEADLINE_MS = 1000;
  // True only during onEnter's blocking build-to-target phase, until the popup has been
  // drawn. Gates showBuildPopup() so the parser's popup callback (which persists into
  // background buildSomeMore chunks) can never draw over a displayed page.
  bool buildPopupPending = false;
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
  void rememberCurrentContentOffset();
  bool saveProgress(int spineIndex, int currentPage, int pageCount);
  // Page-turn path: writes only when the debouncer says the batch is due, or when
  // forceSave is set (a re-layout changed the pagination and must not be left stale).
  bool queueProgressSave(int spineIndex, int currentPage, int pageCount, bool forceSave = false);
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
  void onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action);
  // Opens the reader menu for the current position (short-press Confirm)
  void openReaderMenu();
  void openDictionaryWordSelect();
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
  // Opens the Reading Stats screen for this book plus the all-books totals.
  void openReadingStats();
  // Returns true if sync acted (launched, or surfaced a save error); false if it was a no-op
  // because no KOReader credentials are stored.
  bool launchKOReaderSync();
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
  // Called on every row change inside the Reader Settings screen, via the overlay
  // sink, so the book's sidecar is already correct if the reader is switched off in
  // there. Writes only: prefs_ still holds what the page was laid out with, so the
  // re-layout decision at applyReaderSettingsEdit() is unaffected.
  void persistReaderSettingsEdit(const ReaderPrefs& live) const;
  static void readerEditSinkThunk(void* ctx, const ReaderPrefs& live);
  // Delete this book's override and follow the global settings again.
  // Reads the Customise Status Bar screen's result back into this book's override.
  void applyStatusBarEdit();
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
  void loadCachedBookmarks();
  void addBookmark();
  void updateBookmarkFlag();

  // Footnote navigation
  void navigateToHref(const std::string& href, bool savePosition = false);
  void restoreSavedPosition();

 public:
  explicit EpubReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Epub> epub,
                              int initialRefreshCountdown)
      : Activity("EpubReader", renderer, mappedInput),
        epub(std::move(epub)),
        pagesUntilFullRefresh(initialRefreshCountdown) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  // Full CPU speed + fast loop ticks while a section build runs: at the low-power
  // frequency a giant chapter's background rebuild stretches from ~40s to many
  // minutes, so the reader exits before it can finalize and the next open restarts
  // it from page 0. Reverts to normal power behavior the moment the build finishes,
  // and while the build is heap-paused (no work is happening, so spinning at full
  // speed would only burn battery; the paused gate still retries every loop pass).
  bool skipLoopDelay() override { return section && section->isBuilding() && !buildHeapPaused; }
  bool isReaderActivity() const override { return true; }
  // Arms the power double-click only when the bound function could actually run right
  // now. Every single power click pays the detector's ~280 ms hold-back while it waits
  // for a second one, and a page turn on Power is the thing that lag is most felt on, so
  // a binding that cannot fire on this page (footnotes bound with none here, Go to
  // Paragraph with numbering off, KOSync with no credentials) leaves clicks instant.
  bool isBookContext() const override { return true; }
  // The bindings router hands every gesture here first; the reader answers with the same
  // entry points the pop-up and the long-press bindings already use.
  bool runBoundAction(const uint8_t function) override { return runBoundMenuFunction(function); }
  bool wantsPowerDoubleClick() const override {
    const uint8_t function = SETTINGS.doubleClickPowerFunction;
    switch (function) {
      case CrossPointSettings::LP_MENU_DISABLED:
        return false;
      // These two answer by reading the card, and this runs on EVERY main-loop pass, so
      // they arm unconditionally and report themselves unavailable at the press instead.
      case CrossPointSettings::LP_MENU_VIEW_QUOTES:
      case CrossPointSettings::LP_MENU_WALLPAPER_HOLD:
        return true;
      default:
        return boundMenuFunctionAvailable(function);
    }
  }
  void runPowerDoubleClick() override;
  bool appliesNightMode() const override { return true; }
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
