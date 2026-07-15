#pragma once
#include <EpdFontFamily.h>
#include <Epub.h>
#include <Epub/FootnoteEntry.h>
#include <Epub/Section.h>

#include <optional>
#include <string>
#include <vector>

#include "BookmarkEntry.h"
#include "EndOfBookOptions.h"
#include "EpubReaderMenuActivity.h"
#include "HighlightController.h"
#include "ProgressMapper.h"
#include "activities/Activity.h"
#include "reading_stats/ReaderStatsSession.h"
#include "reading_stats/SdStatsFiles.h"

class Page;

class EpubReaderActivity final : public Activity {
  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  // Incremental background build of the NEXT chapter, pumped a few pages per page
  // turn so its cache is warm (committed) by the time the reader crosses into it.
  // Replaces the old blocking one-shot next-chapter index. Never read while it is
  // building (its single file handle is mid-write); it is only ever committed here
  // and re-loaded fresh on arrival. prefetchSpineIndex_ marks which next chapter is
  // being handled this position (set even when the section pointer is null: warm
  // cache found, or a build that failed to start — so we do not retry every turn).
  std::unique_ptr<Section> prefetchSection_ = nullptr;
  int prefetchSpineIndex_ = -1;
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

  bool buildSectionForRead(Section& section, uint16_t viewportWidth, uint16_t viewportHeight);
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
