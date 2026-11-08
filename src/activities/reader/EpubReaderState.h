#pragma once
#include <Epub.h>
#include <Epub/FootnoteEntry.h>
#include <Epub/Section.h>

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "BookmarkEntry.h"
#include "EndOfBookOptions.h"
#include "ReaderPinchGesture.h"
#include "ReaderPrefs.h"
#include "ReaderProgressSaveDebouncer.h"
#include "ReaderReturnHistory.h"
#include "ReaderUtils.h"
#include "ReadingTime.h"
#include "components/OptionPopup.h"
#include "components/StatusBar.h"
#include "util/ReadingJournal.h"

// What EpubReaderActivity remembers between calls, and the limits that govern its section
// builds. A private base, so the reader's source files use each member by its plain name.
struct EpubReaderState {
  EpubReaderState(std::unique_ptr<Epub> epub, const int initialRefreshCountdown, const bool sortesMode)
      : epub(std::move(epub)),
        sortesMode(sortesMode),
        pendingSortesPage(sortesMode),
        pagesUntilFullRefresh(initialRefreshCountdown) {}

  std::unique_ptr<Epub> epub;
  const bool sortesMode;
  bool pendingSortesPage;
  uint32_t sortesEmptyChapters = 0;
  std::unique_ptr<Section> section = nullptr;
  // Per-book reader "look" settings. Loaded from <cachePath>/reader_override.bin on
  // enter if present (prefsCustom_ = true), else a snapshot of the global settings.
  // The reader lays out exclusively through prefs_, so the global singleton is never
  // mutated for reading and a custom book stays decoupled from global changes.
  ReaderPrefs prefs_;
  int sessionPages = 0;               // Status bar only; reset with this reader activity.
  reading_time::PageTimer pageTimer;  // pace for the time-left items, this sitting only
  reading_journal::Session journal;   // this sitting's line in /stats/journal.txt
  // Book-bar chapter marks, built once on enter (statusbar::addChapterMark). 128 bytes
  // as a member rather than a vector: fixed, and read on the stack-tight render task.
  uint16_t chapterMarks_[statusbar::kMaxChapterMarks] = {};
  int chapterMarkCount_ = 0;
  // Printed-page labels of the current spine and the page each one starts on (page-list
  // books only), rebuilt for each new section and once more when its build settles. Read by the status
  // bar, which appends the label to the page item.
  struct PrintedMark {
    uint16_t page;
    char label[12];
  };
  mutable std::vector<PrintedMark> printedMarks_;
  mutable bool printedMarksStale_ = true;
  mutable bool printedMarksBuilding_ = false;
  bool hasPageList_ = false;
  bool prefsCustom_ = false;
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
  int lastRenderedSpine = -1;  // spine of the page last drawn, for Refresh at Chapter Start
  int cachedSpineIndex = 0;
  int cachedChapterTotalPageCount = 0;
  std::optional<uint32_t> cachedVisibleTextOffset;
  // Visible-codepoint offset of the page currently on screen, captured when the page is loaded
  // (Page::visibleTextOffset). Lets saveProgress persist the offset without reopening section.bin.
  std::optional<uint32_t> currentPageVisibleOffset;
  // Paired with the displayed offset, even if a later destination fails to load.
  int32_t currentPageSpineIndex = -1;
  ReaderReturnHistory returnHistory;
  ReaderPinchGesture pinchGesture;  // two fingers spread or pinched step the text size
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
  bool showBookmarkMessage = false;
  // "No dictionary set" popup, shown when a lookup is triggered without a configured dictionary.
  bool showDictionaryMessage = false;
  unsigned long dictionaryMessageTime = 0UL;
  bool ignoreNextConfirmRelease = false;
  // Idle-time glyph prewarm: after a page settles, scan the neighbour the
  // reader is moving toward (scan mode draws nothing) and load its missing
  // glyphs from SD during idle, so the next turn's in-render prewarm is a cache
  // hit instead of ~100 ms of SD reads on the page-turn critical path. One
  // attempt per position. Latency claim needs device measurement.
  int idlePrewarmSpine = -1;
  int idlePrewarmPage = -1;
  unsigned long lastRenderCompleteMs = 0;
  // Set by the render task once a page is on screen; the loop then remembers the book.
  std::atomic<bool> pageRendered{false};
  bool bookRemembered = false;
  bool autoPullChecked = false;
  // Where this book opened; a different page on exit marks it for Auto Sync's push.
  int openedSpine = -1;
  int openedPage = -1;
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
  // The end-of-book screen swaps out a whole page: its first paint is a HALF, later
  // cursor moves stay FAST. Reset by any book page paint.
  bool endOfBookPainted = false;
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

  // Ordinary page turns are batched instead of writing progress on each one:
  // EpubReaderUtils::saveProgress() is a writeAtomic, several FAT operations for
  // six bytes, and on the X3 that lands between the button press and the page.
  // Flushed on exit, which is also the sleep path (ActivityManager::goToSleep()
  // replaces this activity and so runs onExit()).
  ReaderProgressSaveDebouncer progressSaveDebouncer;
  // Set when the debouncer says a save is due; loop() writes it at the next pause.
  bool progressSaveDue = false;

  // Grayscale strip scratch for the blocking (X3) tier of renderContents(). It used to be
  // allocated and freed on every page render; a whole reading session of that churn measurably
  // decays the largest contiguous block. Kept alive between pages instead and handed back
  // before the heap-hungry work (section builds, leaving the book).
  std::unique_ptr<uint8_t[]> grayscaleStripScratch;
  size_t grayscaleStripScratchBytes = 0;
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
  // Set by dropSectionForRelayout, spent by the next build: a settings change relays out
  // without the deadline Indexing popup.
  bool quietRelayout_ = false;
  // renderer.frameBufferLoanCount() when the framebuffer was last repainted. Differs once a loan
  // (a build step's image probe) has handed the buffer back white under the shown page, so the
  // Quick Menu must not paint straight onto it. Render task only.
  uint32_t pageLoanCount = 0;
  std::string lastSearchQuery;  // offered again the next time Search opens
  // The book's look while the Reader Settings screen edits it (a copy of prefs_).
  ReaderPrefs readerEdit_{};
  // The book's bar while the Customise Status Bar screen edits it (a copy of prefs_'s).
  StatusBarBlock statusBarEdit_{};
};
