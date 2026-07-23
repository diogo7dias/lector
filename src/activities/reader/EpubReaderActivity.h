#pragma once
#include <EpdFontFamily.h>
#include <Epub.h>
#include <Epub/FootnoteEntry.h>
#include <Epub/LayoutParams.h>
#include <Epub/Section.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "BookmarkEntry.h"
#include "EndOfBookOptions.h"
#include "EpubReaderMenuActivity.h"
#include "HighlightController.h"
#include "ProgressMapper.h"
#include "ReaderPrefs.h"
#include "activities/Activity.h"
#include "reading_stats/ReaderStatsSession.h"
#include "reading_stats/SdStatsFiles.h"

class Page;

class EpubReaderActivity final : public Activity {
  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  // Incremental background build of the next chapters, pumped a few pages per page
  // turn so their caches are warm (committed) by the time the reader crosses in.
  // A fixed pool of PREFETCH_AHEAD slots keeps currentSpineIndex+1..+PREFETCH_AHEAD
  // warm; at most ONE slot builds at a time (nearest-first), so peak heap matches
  // the old single-slot prefetch (each build holds a parser + layout arena — two
  // at once would blow the 380KB heap). A slot is never read while building (its
  // file handle is mid-write); it is only committed here and re-loaded fresh on
  // arrival. spineIndex == -1 = empty; handled = the target is resolved (warm,
  // committed, or a build that failed to start — do not re-probe every turn);
  // section != null = an active or just-committed build (holds the ".part" handle).
  //
  // PREFETCH_AHEAD == 0 (Diogo, 2026-07-21): index only the CURRENT chapter. On a
  // single RISC-V core, background prefetch/warm builds contended with input+render
  // during a page turn, so presses could sit dead for seconds while a build ran. The
  // next chapter now builds on demand — when the reader crosses into it, render's
  // loadSectionFromCache miss triggers a foreground sliced build with the progress
  // bar. The slot pool + pump code is kept intact (empty at size 0) for an easy
  // revert; raise this back to a small N to re-enable lookahead.
  static constexpr int PREFETCH_AHEAD = 0;
  struct PrefetchSlot {
    std::unique_ptr<Section> section = nullptr;
    int spineIndex = -1;
    bool handled = false;
  };
  std::array<PrefetchSlot, PREFETCH_AHEAD> prefetchSlots_;
  // Building a section runs the HTML parser + layout arena; background prefetch/warm
  // stays out of the way when the heap is already tight (matches the low-memory
  // render ladder — this work is strictly optional).
  static constexpr uint32_t WARM_MIN_FREE_HEAP = 40000;
  // Whole-book background warmer: once the next-chapter prefetch is idle, the
  // pump probes the remaining chapters one per turn and incrementally builds
  // any that are cold for the current layout settings, so chapter jumps land
  // warm (big after a settings change, where only visited chapters get
  // rebuilt otherwise). One build at a time, sharing the prefetch pump slot.
  // The scan restarts when the layout-settings fingerprint changes.
  std::unique_ptr<Section> warmSection_ = nullptr;
  int warmBuildSpine_ = -1;  // spine warmSection_ is building (-1 none)
  int warmScanSpine_ = -1;   // next spine to probe (-1 = scan not started)
  bool warmScanComplete_ = false;
  uint32_t warmSettingsHash_ = 0;
  // Low-memory render fallback ladder: the current degrade tier for this open book
  // (0 = full quality). Raised when a chapter build aborts for low memory and a
  // reduced-quality rebuild succeeds; it then sticks for the rest of the session so
  // later chapters start already-degraded instead of re-descending each time. Reset
  // to 0 in onEnter when a book is opened. See LowMemoryRenderTier.h.
  int lowMemoryTierFloor_ = 0;

  // Reader-tab layout settings this book renders with. For a "custom" book this is
  // its own snapshot loaded from <cachePath>/reader_override.bin (decoupled from
  // the global settings); otherwise it is a snapshot of the current global values
  // taken at open. ALL layout reads in the reader go through prefs_ so the global
  // CrossPointSettings singleton is never mutated for a per-book override. See
  // ReaderPrefs. Populated in onEnter() by loadReaderPrefs().
  ReaderPrefs prefs_;
  bool prefsCustom_ = false;  // true when this book has its own reader_override.bin
  std::string readerOverridePath() const { return epub->getCachePath() + "/reader_override.bin"; }
  // Load prefs_ for this book: the per-book sidecar if present + valid (sets
  // prefsCustom_), else a snapshot of the current global reader settings.
  void loadReaderPrefs();
  // Persist prefs to this book's reader_override.bin sidecar.
  bool writeReaderOverride(const ReaderPrefs& p) const;
  // Result callback from the in-book Reader-settings screen: capture edits, mark
  // the book custom on any change, persist + re-layout.
  void applyReaderSettingsEdit();

  // Copy another book's reader settings onto this book (Steal Look). sourceCachePath
  // is the chosen book's cache dir, holding its reader_override.bin.
  void applyStolenLook(const std::string& sourceCachePath);

  // Apply a per-book Paperback Look change returned from the reader menu.
  void applyPaperbackLook(uint8_t body, uint8_t status);

  // Draw the optional paragraph-number margin marks over a rendered page.
  void drawParagraphNumbers(const Page& page, int marginLeft, int contentTop);
  // Reset action: delete the sidecar, follow global settings again, re-layout.
  void resetReaderPrefsToGlobal();
  // Drop cached sections so render() rebuilds the current chapter under new prefs_.
  void reloadForReaderPrefsChange();

  // Sliced foreground build of the on-screen chapter. On a cache miss the ~13s
  // layout is driven a few pages per render() call instead of one blocking pass,
  // so the render lock frees between slices (input stays live, Back cancels) and
  // a progress bar is painted. Only the foreground miss build; prefetch/warm are
  // separate one-shot pumps. slicedBuildBase_ captures the layout key so each slice
  // and each low-memory tier restart re-issues Section::startBuild() identically.
  // Tier-0 (full quality) layout key for the in-flight sliced build, captured once
  // at beginSlicedBuild; a low-memory tier is applied on top per attempt (withTier).
  LayoutParams slicedBuildBase_;
  bool sectionBuildActive_ = false;                  // a sliced foreground build is in flight
  int sectionBuildTier_ = 0;                         // render tier currently being built
  unsigned long sectionBuildProgressPaintedMs_ = 0;  // last progress-bar refresh (throttle)
  int sectionBuildProgressPercent_ = -100;           // last painted bar % (repaint only when it moves)
  // Random /sleep .pxc drawn behind the indexing face so the wait shows a
  // wallpaper instead of a blank page. Empty = plain banner+bar face. Picked
  // once per sliced build via a side-effect-free peek into the wallpaper index
  // (never advances the sleep rotation cursor, never triggers an index build).
  std::string indexingBackdropPath_;

  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  std::optional<uint16_t> pendingPageJump;
  // Set when navigating to a footnote href with a fragment (e.g. #note1).
  // Cleared on the next render after the new section loads and resolves it to a page.
  std::string pendingAnchor;
  int pagesUntilFullRefresh = 0;
  int cachedSpineIndex = 0;
  int cachedChapterTotalPageCount = 0;
  unsigned long lastPageTurnTime = 0UL;
  unsigned long pageTurnDuration = 0UL;
  // Signals that the next render should reposition within the newly loaded section
  // based on a cross-book percentage jump.
  bool pendingPercentJump = false;
  // Normalized 0.0-1.0 progress within the target spine item, computed from book percentage.
  float pendingSpineProgress = 0.0f;
  bool pendingScreenshot = false;
  bool pendingSyncSaveError = false;
  bool skipNextButtonCheck = false;  // Skip button processing for one frame after subactivity exit
  bool automaticPageTurnActive = false;
  // Page press that arrived while the section was still loading (e.g. the first
  // page build after a wake, when the restored frame is already visible but the
  // book engine is not ready). -1 = back, +1 = forward, 0 = none. loop() replays
  // it once the section is ready; latest press wins and only one is replayed.
  // Wake diagnostics (SETTINGS.wakeDiagnostics): one-shot unlock-to-usable
  // timing, drawn over the first ready page and always logged.
  unsigned long diagEnterMs_ = 0;
  std::string diagMissInfo_;
  unsigned long diagSectionReadyMs_ = 0;
  unsigned long diagFirstPressMs_ = 0;
  bool diagSectionWasBuild_ = false;
  bool diagOverlayPending_ = false;

  int8_t queuedPageTurn = 0;
  unsigned long queuedPageTurnAtMs = 0UL;
  bool showBookmarkMessage = false;
  bool ignoreNextConfirmRelease = false;
  // The in-book Reader-settings overlay (SettingsActivity) finishes on the Back PRESS
  // edge; the matching release lands back in this reader's loop. Set on return from that
  // overlay so the reader swallows that one stale Back release instead of treating it as
  // its own Back (which would cancel the fresh re-index and jump home). See loop() and
  // applyReaderSettingsEdit(); mirrors OpdsBookBrowserActivity::consumeBack.
  bool ignoreBackUntilRelease_ = false;
  bool currentPageBookmarked = false;
  bool bookmarkRemoved = false;  // true when last toggle removed (controls popup text)
  std::vector<BookmarkEntry> cachedBookmarks;
  // Tracks whether this book is currently removed from Recent Books by the
  // removeReadBooksFromRecents feature (set at End-of-Book, cleared if paged back in).
  bool recentsEntryRemoved = false;
  unsigned long bookmarkMessageTime = 0UL;
  // Set when the reader is left at end-of-book and SETTINGS.moveFinishedToReadFolder is on.
  // Consumed in onExit() to relocate the finished book into /Read/.
  bool pendingReadFolderMove = false;
  // Next-book suggestion menu for the End-of-Book screen
  EndOfBookOptions endOfBookOptions;
  reading_stats::SdStatsFiles statsFiles;
  reading_stats::ReaderStatsSession statsSession{statsFiles};
  bool statsTrackingActive = false;

  // Footnote support
  std::vector<FootnoteEntry> currentPageFootnotes;
  struct SavedPosition {
    int spineIndex;
    int pageNumber;
  };
  static constexpr int MAX_FOOTNOTE_DEPTH = 3;
  SavedPosition savedPositions[MAX_FOOTNOTE_DEPTH] = {};
  int footnoteDepth = 0;

  // Last position persisted by render()'s saveProgress, used to skip redundant
  // writeAtomic calls on no-op re-renders (menu/bookmark/screenshot).
  int lastSavedSpineIndex = -1;
  int lastSavedPage = -1;
  int lastSavedPageCount = -1;

  void renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
  void renderStatusBar() const;
  void pumpNextChapterPrefetch(uint16_t viewportWidth, uint16_t viewportHeight);
  void pumpWholeBookWarm(uint16_t viewportWidth, uint16_t viewportHeight);
  // Loads or builds `section` for reading, descending the low-memory render tier
  // ladder (LowMemoryRenderTier.h) when a build aborts for lack of memory. Returns
  // false only on a genuine (non-memory) build failure or when even the lowest tier
  // cannot fit. Updates lowMemoryTierFloor_ on a successful degrade.
  // Wake diagnostics: why did the section cache miss — was the file absent or
  // stale, and does another kept generation still hold this spine (the
  // wrong-drawer signature of the low-memory tier ladder or a layout-param
  // wobble)? Cheap: one exists() plus a scan of the <=3 generation dirs.
  std::string classifySectionMiss(const Section& section) const;

  // Fast synchronous cache load for `section`: the floor tier first, then a probe
  // of degraded-tier cache generations. Returns true if a cache was adopted (the
  // section is ready to position + paint); false means the chapter must be built.
  bool loadSectionFromCache(Section& section, uint16_t viewportWidth, uint16_t viewportHeight);
  // Build the tier-0 (full quality) LayoutParams for the current book prefs + viewport.
  // Syncs the SD font manager to prefs_ FIRST (ensureLoadedFor) so the resolved fontId
  // always matches the font that will render — this is what makes an in-book font/size
  // change actually take effect instead of reloading a stale cached layout. Every
  // section cache/build call in the reader goes through this, so the font can never be
  // unsynced when the cache key is formed. A low-memory tier is layered on with withTier().
  LayoutParams layoutParamsBase(uint16_t viewportWidth, uint16_t viewportHeight);
  // Prime the first-tier sliced build after a cache miss. Captures layout params in
  // slicedBuildBase_, sets sectionBuildActive_, paints the initial (0%) progress face and
  // requests the first slice. Returns false on a hard start failure.
  bool beginSlicedBuild(uint16_t viewportWidth, uint16_t viewportHeight);
  // (Re)start Section::startBuild() at render tier `tier` under a framebuffer loan,
  // using slicedBuildBase_. Used to prime the build and to restart it a tier lower on a
  // low-memory abort. Returns false when startBuild fails at that tier.
  bool startBuildAtTier(int tier);
  // Sets indexingBackdropPath_ to a random /sleep .pxc via an O(1) peek into the
  // prebuilt wallpaper index. Returns false (path empty) when no index/candidate
  // exists — the indexing face then stays the plain banner+bar.
  bool pickIndexingBackdrop();
  // Advance the in-flight sliced build by one slice (a few pages) under a per-slice
  // framebuffer loan, descending the low-memory tier on OOM and painting progress.
  // Returns true only when the build just completed (section positioned, ready to
  // paint this render); false while still building or after a cancel/error bail.
  bool advanceSectionBuild();
  // Paint the "Indexing" progress bar for the current build fraction. Throttled to a
  // few refreshes across a build unless `force` (initial face, tier change, done).
  void paintBuildProgress(bool force);
  // Resolve the on-screen page once a section is ready (pendingPageJump / nextPage /
  // anchor / cached-progress / percent jump). Runs after a cache hit or a completed
  // sliced build. Requires final section->pageCount.
  void positionAfterSectionReady();
  // Replace the on-screen "Indexing" popup with an explicit build-failure popup
  // (corrupt EPUB, or OOM even at the lowest tier) so a failed build does not hang
  // on a stale progress face. Clears the screen first.
  void showBuildError();
  // If a chapter build is currently monopolizing the render task, ask it to stop
  // so the render lock frees in ~one parse step (or the next slice bails). Call
  // before any "abandon this chapter" press path (go home, file browser, chapter
  // skip) so it acts promptly instead of blocking for the full ~13s build. No-op
  // when no build is in flight. Does not itself take the render lock.
  void cancelInFlightBuild();
  bool saveProgress(int spineIndex, int currentPage, int pageCount);
  // Jump to a percentage of the book (0-100), mapping it to spine and page.
  void jumpToPercent(int percent);
  void onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action);
  // Returns true if sync acted (launched, or surfaced a save error); false if it was a no-op
  // because no KOReader credentials are stored.
  bool launchKOReaderSync();
  void applyOrientation(uint8_t orientation);
  void toggleAutoPageTurn(uint8_t selectedPageTurnOption);
  void pageTurn(bool isForwardTurn);
  void loadCachedBookmarks();
  void addBookmark();
  void updateBookmarkFlag();

  // Footnote navigation
  void navigateToHref(const std::string& href, bool savePosition = false);
  void restoreSavedPosition();

  // ── Grab-quote / highlight selection (ported from DX34) ──────────────────
  // Per-word geometry + text for the selection overlay. y is per-line (shared
  // by all words on the line); x/width are per-word.
  struct WordInfo {
    int x = 0;
    int y = 0;
    int width = 0;
    std::string text;
    EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  };
  crosspoint::reader::HighlightController highlights_;

  std::vector<WordInfo> buildWordList(const Page& page, int xOffset, int yOffset, int fontId) const;
  void rebuildHighlightWordCache(int xOffset, int yOffset);
  void enterHighlightMode();
  void exitHighlightMode();
  void highlightMoveCursor(int direction);
  void highlightMoveCursorLine(int direction);
  void highlightConfirmSelection();
  void handleHighlightInput();
  void loopHighlightMode();
  void renderHighlights(const Page& page, int fontId, int xOffset, int yOffset);
  std::string extractQuoteText();
  std::string getChapterTitle() const;
  std::string getQuotesFilePath() const;
  void saveQuoteToFile(const std::string& quote);

 public:
  explicit EpubReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Epub> epub)
      : Activity("EpubReader", renderer, mappedInput), epub(std::move(epub)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  bool isReaderActivity() const override { return true; }
  ScreenshotInfo getScreenshotInfo() const override;
  CrossPointPosition getCurrentPosition() const;
};
