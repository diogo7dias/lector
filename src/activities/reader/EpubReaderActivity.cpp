#include "EpubReaderActivity.h"

#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_system.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>

#include "../../util/BookmarkFile.h"
#include "BookInfoActivity.h"
#include "BookStatsActivity.h"
#include "BookmarkEntry.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "DictionaryWordSelectActivity.h"
#include "EpubReaderBookmarksActivity.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "EpubReaderFootnotesActivity.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "EpubReaderUtils.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncActivity.h"
#include "MappedInputManager.h"
#include "ParagraphNumberLayout.h"
#include "ProgressMapper.h"
#include "QrDisplayActivity.h"
#include "QuoteSelectActivity.h"
#include "QuoteText.h"
#include "QuoteUnderline.h"
#include "QuotesViewerActivity.h"
#include "ReaderPresetStore.h"
#include "ReaderPresetsActivity.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "StealLookActivity.h"
#include "activities/settings/TextSettingsActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "reading_stats/ReadingStatsClock.h"
#include "reading_stats/ReadingStatsPresentation.h"
#include "sleep/SleepPauseToggle.h"
#include "util/BookCacheUtils.h"
#include "util/BookFiling.h"
#include "util/BookmarkUtil.h"
#include "util/BoundMenuLabels.h"
#include "util/DeferredFavorite.h"
#include "util/FavoriteImage.h"
#include "util/ScreenshotUtil.h"

namespace {
// pagesPerRefresh now comes from SETTINGS.getRefreshFrequency()
// pages per minute, first item is 1 to prevent division by zero if accessed
constexpr int PAGE_TURN_RATES[] = {1, 1, 3, 6, 12};
constexpr size_t initialBookmarkCacheCapacity = 16;
constexpr float bookmarkProgressEpsilon = 0.0001f;
// paragraph_counts.bin: [uint8 version][uint16 spineCount][uint16 count]*spineCount.
constexpr uint8_t PARAGRAPH_COUNTS_VERSION = 1;
// Quote underlines. Positions only: 128 anchors is 1.5KB resident, and a book with
// more saved quotes than that simply stops underlining the extras.
constexpr size_t MAX_QUOTE_ANCHORS = 128;
// Heap that must remain free beyond the sidecar itself before it is read at all.
constexpr size_t QUOTE_INDEX_HEAP_HEADROOM = 8 * 1024;
// Ceiling on the word list flattened per page while looking for a quote. A full
// page of small type is well under this; the cap only bounds the allocation.
constexpr size_t MAX_PAGE_TOKENS = 512;

int clampPercent(int percent) {
  if (percent < 0) {
    return 0;
  }
  if (percent > 100) {
    return 100;
  }
  return percent;
}

using bookfiling::isInFolder;
using bookfiling::READ_FOLDER;
using bookfiling::RECENTS_FOLDER;
using bookfiling::ROOT_FOLDER;

bool isInReadFolder(const std::string& path) { return isInFolder(path, READ_FOLDER); }

struct ProgressRange {
  float start;
  float end;
};

ProgressRange getPageProgressRange(const std::shared_ptr<Epub>& epub, const int spineIndex, const int page,
                                   const int pageCount) {
  if (pageCount <= 1) {
    return {epub->calculateProgress(spineIndex, 0.0f), epub->calculateProgress(spineIndex, 1.0f)};
  }

  const float step = 1.0f / static_cast<float>(pageCount - 1);
  const float anchor = std::clamp(static_cast<float>(page) * step, 0.0f, 1.0f);
  const float start = std::max(0.0f, anchor - (step * 0.5f));
  const float end = std::min(1.0f, anchor + (step * 0.5f));
  return {epub->calculateProgress(spineIndex, start), epub->calculateProgress(spineIndex, end)};
}

bool bookmarkMatchesProgress(const BookmarkEntry& bookmark, const int spineIndex, const int page, const int pageCount,
                             const ProgressRange& pageRange) {
  if (bookmark.computedSpineIndex == spineIndex && bookmark.computedChapterPageCount == pageCount &&
      bookmark.computedChapterProgress == page) {
    return true;
  }

  const float bookmarkProgress = std::clamp(bookmark.percentage, 0.0f, 1.0f);
  return bookmarkProgress + bookmarkProgressEpsilon >= pageRange.start &&
         bookmarkProgress - bookmarkProgressEpsilon <= pageRange.end;
}

}  // namespace

void EpubReaderActivity::onEnter() {
  Activity::onEnter();

  if (!epub) {
    return;
  }

  ImageBlock::clearSessionRenderFailures();
  // Lazy image extraction: section builds only header-probe images, so the first
  // render of an image page pulls the file out of the EPUB through this hook.
  ImageBlock::setExtractor(epub.get(), [](void* ctx, const char* src, const char* dest) {
    return static_cast<Epub*>(ctx)->extractItemToFile(src, dest);
  });

  // Configure screen orientation based on settings
  // NOTE: This affects layout math and must be applied before any render calls.
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  epub->setupCacheDir();

  // Reading stats. Latched here rather than read per event so a mid-book toggle
  // cannot produce a session that is half tracked. The cache dir must already
  // exist: this book's stats file lives inside it.
  statsTrackingActive = SETTINGS.readingStatsEnabled != 0;
  if (statsTrackingActive) {
    statsSession.configure({.idleThresholdSeconds = SETTINGS.readingStatsIdleSeconds(),
                            .minimumPageSeconds = 2,
                            .minimumSessionSeconds = 60});
    statsSession.begin(epub->getCachePath(), reading_stats::currentLocalDateTime());
  }

  // Load this book's per-book reader settings (or a snapshot of global) before any
  // layout, so the first render already paginates through the right ReaderPrefs.
  loadReaderPrefs();
  // Only one SD font size is resident at a time, and the id resolver returns whichever
  // that is regardless of the size asked for, so a book whose prefs differ from the
  // global reader selection has to make its own size resident here. Without this the
  // book laid out at the global size while its prefs (and the settings screen) said
  // otherwise, and every open rebuilt the section cache against the mismatched id.
  // No-op when the book's family and size already match what is loaded.
  sdFontSystem.ensureLoadedFor(renderer, prefs_.sdFontFamilyName, prefs_.fontPointSize);
  // Per-spine paragraph counts for whole-book numbering (sized to the spine; filled
  // from the sidecar if present, else zeros that fill in as the book is read).
  loadParagraphCounts();
  // Where this book's saved quotes sit, so their underlines can be drawn back in.
  loadQuoteAnchors();

  HalFile f;
  if (Storage.openFileForRead("ERS", epub->getCachePath() + "/progress.bin", f)) {
    uint8_t data[10];
    int dataSize = f.read(data, sizeof(data));
    if (dataSize == 4 || dataSize == 6 || dataSize == 10) {
      currentSpineIndex = data[0] + (data[1] << 8);
      nextPageNumber = data[2] + (data[3] << 8);
      if (nextPageNumber == UINT16_MAX) {
        // UINT16_MAX is an in-memory navigation sentinel for "open previous
        // chapter on its last page". It should never be treated as persisted
        // resume state after sleep or reopen.
        LOG_DBG("ERS", "Ignoring stale last-page sentinel from progress cache");
        nextPageNumber = 0;
      }
      cachedSpineIndex = currentSpineIndex;
      LOG_DBG("ERS", "Loaded cache: %d, %d", currentSpineIndex, nextPageNumber);
    }
    if (dataSize == 6) {
      cachedChapterTotalPageCount = data[4] + (data[5] << 8);
    } else if (dataSize == 10) {
      cachedChapterTotalPageCount = data[4] + (data[5] << 8);
      cachedVisibleTextOffset = static_cast<uint32_t>(data[6]) | (static_cast<uint32_t>(data[7]) << 8) |
                                (static_cast<uint32_t>(data[8]) << 16) | (static_cast<uint32_t>(data[9]) << 24);
    }
  }
  // We may want a better condition to detect if we are opening for the first time.
  // This will trigger if the book is re-opened at Chapter 0.
  if (currentSpineIndex == 0) {
    int textSpineIndex = epub->getSpineIndexForTextReference();
    if (textSpineIndex != 0) {
      currentSpineIndex = textSpineIndex;
      cachedVisibleTextOffset.reset();
      LOG_DBG("ERS", "Opened for first time, navigating to text reference at index %d", textSpineIndex);
    }
  }

  // Save current epub as last opened epub and add to recent books
  APP_STATE.openEpubPath = epub->getPath();
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), epub->getThumbBmpPath());

  loadCachedBookmarks();

  // Trigger first update
  requestUpdate();
}

void EpubReaderActivity::onExit() {
  Activity::onExit();

  // Leaving the book is one of the two moments queued favourite renames run (the
  // other is sleep entry). Fire-and-forget: the worker's directory scans overlap
  // this exit's own card work instead of ever standing between a press and a page.
  DeferredFavorite::flush();

  // Stop speaking for this book: everything outside the reader follows the global
  // status bar setting again.
  SETTINGS.clearStatusBarOverride();

  if (statsTrackingActive) {
    statsSession.pause(millis());
    if (!statsSession.finish()) LOG_ERR("RSTAT", "Failed to save EPUB reading stats");
  }

  // The extractor holds a raw pointer to this activity's epub; drop it before
  // the activity (and the shared_ptr) goes away.
  ImageBlock::setExtractor(nullptr, nullptr);

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();

  // Persist whole-book paragraph counts gathered this session (epub still valid here).
  if (epub) saveParagraphCounts();

  // Update this book's home-list progress badge from the current position. One write
  // per reading session (setProgress skips if unchanged), so no page-turn cost.
  if (epub) {
    const int curPage = section ? section->currentPage : nextPageNumber;
    const int pageCnt = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
    const float chapterProgress = pageCnt > 0 ? static_cast<float>(curPage) / static_cast<float>(pageCnt) : 0.0f;
    const int pct =
        clampPercent(static_cast<int>(epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f + 0.5f));
    RECENT_BOOKS.setProgress(epub->getPath(), pct);
  }

  // Leaving mid-footnote loses the in-RAM return stack on deep sleep; persist the
  // pre-footnote position so the book reopens at the link origin, not the footnote.
  if (footnoteDepth > 0 && epub) {
    const SavedPosition& origin = savedPositions[0];
    saveProgress(origin.spineIndex, origin.pageNumber, 0);
  }

  section.reset();
  // File the book on the way out: finished books go to /read, anything else that was
  // opened goes to /recents. A finished move wins, so a book never lands in both. The
  // move is done after the Epub is released so no handle is open across the rename.
  const char* fileInto = nullptr;
  if (epub && !pendingDeleteBook) {
    if (pendingRemoveFromRecents) {
      // Removing undoes the filing that opening the book did, so the file goes back to
      // the card root. A book that was never filed stays where the user put it.
      if (isInFolder(epub->getPath(), RECENTS_FOLDER)) fileInto = ROOT_FOLDER;
    } else if (pendingReadFolderMove) {
      fileInto = READ_FOLDER;
    } else if (SETTINGS.moveOpenedToRecentsFolder && !isInFolder(epub->getPath(), RECENTS_FOLDER)) {
      fileInto = RECENTS_FOLDER;
    }
  }
  std::string finalPath = epub ? epub->getPath() : std::string();
  if (fileInto) {
    const std::string srcPath = epub->getPath();
    const std::string oldCachePath = epub->getCachePath();
    const std::string dstPath = bookfiling::buildFolderDestination(srcPath, fileInto);
    epub.reset();  // release the Epub (and any open handles) before renaming on the SD card
    finalPath = bookfiling::moveBookToFolder(srcPath, dstPath, oldCachePath);
  } else {
    epub.reset();
  }

  if (pendingDeleteBook && !finalPath.empty()) {
    if (Storage.remove(finalPath.c_str())) {
      // The cache directory is keyed on the book path, so leaving it behind would
      // strand a layout cache and a progress file nothing will open again.
      clearBookCache(finalPath);
      RECENT_BOOKS.removeByPath(finalPath);
      if (APP_STATE.openEpubPath == finalPath) {
        APP_STATE.openEpubPath.clear();
        APP_STATE.saveToFile();
      }
    } else {
      LOG_ERR("ERS", "Failed to delete book: %s", finalPath.c_str());
    }
  }

  if (pendingRemoveFromRecents && !finalPath.empty()) {
    RECENT_BOOKS.removeByPath(finalPath);
    // Drop the resume pointer too, or Back on the home screen would reopen the very
    // book the user just removed.
    if (APP_STATE.openEpubPath == finalPath) {
      APP_STATE.openEpubPath.clear();
      APP_STATE.saveToFile();
    }
  }
}

void EpubReaderActivity::openReaderMenu() {
  const int currentPage = section ? section->currentPage + 1 : 0;
  const int totalPages = section ? section->estimatedTotalPages() : 0;
  float bookProgress = 0.0f;
  if (epub->getBookSize() > 0 && section && section->estimatedTotalPages() > 0) {
    const float chapterProgress =
        static_cast<float>(section->currentPage) / static_cast<float>(section->estimatedTotalPages());
    bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
  }
  const int bookProgressPercent = clampPercent(static_cast<int>(bookProgress + 0.5f));
  // Resolve the current chapter name for the menu header, falling back to "Unnamed"
  // when the book has no TOC entry for this spine index.
  std::string chapterName = tr(STR_UNNAMED);
  const int menuTocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (menuTocIndex != -1) {
    chapterName = epub->getTocItem(menuTocIndex).title;
  }
  // Settle any favorite rename queued the last time this menu was open, so the row below
  // reads the true state and a rename that failed has already given the reference back.
  // The worker has almost always finished by now; this only applies its outcome.
  DeferredFavorite::reconcile();
  // Wallpaper triage targets the file the last sleep screen actually rendered.
  // Only offer it while that file is still on the card: the user may have deleted
  // or moved it from the file browser since.
  const std::string& lastWallpaper = APP_STATE.lastSleepWallpaperPath;
  const bool hasSleepWallpaper = !lastWallpaper.empty() && Storage.exists(lastWallpaper.c_str());
  const bool wallpaperFavorited = hasSleepWallpaper && FavoriteImage::isFavoritePath(lastWallpaper);
  // A fixed /sleep.pxc or /sleep.bmp has nowhere to be paused to — it is not part
  // of a rotation folder.
  const bool wallpaperPausable = hasSleepWallpaper && crosspoint::sleep::isUnderSleepDirs(lastWallpaper);
  // "View Quotes" is only offered once Grab Quote has actually written a sidecar for
  // this book; an empty viewer would be a dead row.
  const bool hasQuotes = Storage.exists(quote_text::quotesFilePathFor(epub->getPath()).c_str());
  startActivityForResult(
      std::make_unique<EpubReaderMenuActivity>(
          renderer, mappedInput, epub->getTitle(), epub->getAuthor(), chapterName, currentPage, totalPages,
          bookProgressPercent, SETTINGS.orientation, !currentPageFootnotes.empty(), !cachedBookmarks.empty(),
          prefsCustom_, prefs_.paragraphNumbering, prefs_.paragraphNumberSize, prefs_.paperbackLookBody,
          prefs_.paperbackLookStatus, prefs_.statusBarEnabled, SETTINGS.sbOffBar, hasSleepWallpaper, wallpaperFavorited,
          wallpaperPausable, hasQuotes),
      [this](const ActivityResult& result) {
        // Always apply orientation / paragraph-number / paperback changes even if cancelled
        const auto& menu = std::get<MenuResult>(result.data);
        applyOrientation(menu.orientation);
        toggleAutoPageTurn(menu.pageTurnOption);
        applyParagraphNumbering(menu.paragraphNumbering, menu.paragraphNumberSize);
        applyPaperbackLook(menu.paperbackBody, menu.paperbackStatus);
        // Last of the live toggles because it is the only one that repaginates.
        applyStatusBar(menu.statusBar, menu.progressBar);
        // A hold inside the menu comes back cancelled with the bound function attached:
        // no row was chosen, so this replaces the row action rather than following it.
        //
        // Runs AFTER the toggles above, never before. Grab Quote hands the picker a raw
        // Section* and the picker outlives this callback; starting it first and then
        // letting applyStatusBar/applyOrientation drop the section would leave that
        // pointer dangling. In this order the worst case is a bound function that
        // declines because the section is already gone, which every one of them handles.
        if (menu.holdFunction != CrossPointSettings::LP_MENU_DISABLED) {
          runBoundMenuFunction(menu.holdFunction);
          return;
        }
        if (!result.isCancelled) {
          onReaderMenuConfirm(static_cast<EpubReaderMenuActivity::MenuAction>(menu.action));
        }
      });
}

bool EpubReaderActivity::buildTickHeapGate() {
  const size_t freeHeap = ESP.getFreeHeap();
  const size_t maxBlock = ESP.getMaxAllocHeap();
  // Below the floors: just wait. The tick is deferrable — page-turn transients
  // free up between turns and the tick retries every loop pass. Track the
  // paused state so skipLoopDelay() stops pinning the CPU at full speed while
  // no build work is actually happening (the gate can stay closed for a long
  // stretch if the retained build context itself holds the heap down).
  buildHeapPaused = freeHeap < BACKGROUND_BUILD_MIN_FREE_HEAP || maxBlock < BACKGROUND_BUILD_MIN_MAX_ALLOC;
  return !buildHeapPaused;
}

void EpubReaderActivity::showBuildPopup() {
  // Mid-build indexing popup: only during onEnter's blocking build-to-target phase
  // (buildPopupPending), at most once, and only when the framebuffer isn't on loan.
  // If it fires while the loan is active (e.g. the parser's size-based call during
  // startBuild), pending stays set and the deadline check retries after the loan.
  if (!buildPopupPending || !renderer.hasFrameBuffer()) return;
  GUI.drawPopup(renderer, tr(STR_INDEXING));
  scheduleGhostCleanup();
  // HALF-clear the popup when the page replaces it, else "INDEXING" ghosts.
  buildPopupPending = false;
}

void EpubReaderActivity::computeReaderMargins(int& top, int& right, int& bottom, int& left) const {
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  // Uniform margins use screenMargin on every side; otherwise top/bottom are
  // independent while screenMargin stays the horizontal (left/right) margin.
  const uint8_t topMargin = prefs_.uniformMargins ? prefs_.screenMargin : prefs_.screenMarginTop;
  const uint8_t bottomMargin = prefs_.uniformMargins ? prefs_.screenMargin : prefs_.screenMarginBottom;
  top += topMargin;
  bottom += bottomMargin;
  if (prefs_.dynamicMargins) {
    // Auto-widen the horizontal margins toward a target ~62 characters per line,
    // using the reader font's average glyph width as the yardstick. Floored at
    // 10px (mode 1) or 20px (mode 2) and capped at 55px so a narrow orientation
    // keeps a usable viewport. Replaces the fixed horizontal margin; the changed
    // viewport width re-paginates via the section cache like any margin change.
    const int fontId = SETTINGS.getReaderFontId(prefs_);
    const int sampleWidth = renderer.getTextWidth(fontId, "abcdefghijklmnopqrstuvwxyz");
    const int avgCharWidth = (sampleWidth > 0) ? sampleWidth / 26 : 8;
    const int targetTextWidth = 62 * avgCharWidth;
    const int availableWidth = renderer.getScreenWidth() - left - right;
    const int minDynamicMargin = (prefs_.dynamicMargins >= 2) ? 20 : 10;
    const int dynamicMargin = std::max(minDynamicMargin, std::min(55, (availableWidth - targetTextWidth) / 2));
    left += dynamicMargin;
    right += dynamicMargin;
  } else {
    left += prefs_.screenMargin;
    right += prefs_.screenMargin;
  }
}

void EpubReaderActivity::openDictionaryWordSelect() {
  if (SETTINGS.dictionaryName[0] == '\0') {
    showDictionaryMessage = true;
    dictionaryMessageTime = millis();
    requestUpdate();
    return;
  }
  if (!section) return;
  auto page = section->loadPage(section->currentPage);
  if (!page) return;

  // Word geometry must match render(): use the same per-book reader margins.
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  computeReaderMargins(orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);

  startActivityForResult(std::make_unique<DictionaryWordSelectActivity>(renderer, mappedInput, std::move(page),
                                                                        orientedMarginLeft, orientedMarginTop),
                         [this](const ActivityResult&) { requestUpdate(); });
}

void EpubReaderActivity::openReadingStats() {
  // Stop the clock first: time spent staring at the stats screen is not reading.
  if (statsTrackingActive) statsSession.pause(millis());

  const reading_stats::ReadingStatsData book = statsSession.bookSnapshot();
  const reading_stats::ReadingStatsData global = statsSession.globalSnapshot();
  float bookProgress = 0.0f;
  if (epub && epub->getBookSize() > 0 && section && section->estimatedTotalPages() > 0) {
    const float chapterProgress =
        static_cast<float>(section->currentPage) / static_cast<float>(section->estimatedTotalPages());
    bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
  }
  const uint8_t progress = static_cast<uint8_t>(clampPercent(static_cast<int>(bookProgress + 0.5f)));

  startActivityForResult(std::make_unique<BookStatsActivity>(
                             renderer, mappedInput, epub ? epub->getTitle() : std::string{}, book, global, progress,
                             reading_stats::estimateTimeLeft(book.totalReadingSeconds, progress),
                             [this](const bool resetAll, reading_stats::ReadingStatsData& nextBook,
                                    reading_stats::ReadingStatsData& nextGlobal) {
                               const auto now = reading_stats::currentLocalDateTime();
                               const bool reset = resetAll ? statsSession.resetAll(now) : statsSession.resetBook(now);
                               if (reset) {
                                 nextBook = statsSession.bookSnapshot();
                                 nextGlobal = statsSession.globalSnapshot();
                               }
                               return reset;
                             }),
                         [this](const ActivityResult&) { requestUpdate(); });
}

void EpubReaderActivity::openQuoteGrab() {
  if (!section) return;

  // Word geometry must match render(): use the same per-book reader margins.
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  computeReaderMargins(orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);

  // Lay out the picker with this book's actual reader font (per-book prefs), so
  // the highlight boxes line up with the rendered glyphs.
  const int readerFontId = SETTINGS.getReaderFontId(prefs_);
  startActivityForResult(
      // The picker loads its own pages: a quote may run past this one, and it turns
      // pages itself while the reader stays suspended and its section stays alive.
      std::make_unique<QuoteSelectActivity>(renderer, mappedInput, section.get(), section->currentPage,
                                            orientedMarginLeft, orientedMarginTop, epub, currentSpineIndex,
                                            readerFontId),
      [this](const ActivityResult&) {
        loadQuoteAnchors();  // a fresh grab appended to the sidecar
        requestUpdate();
      });
}

bool EpubReaderActivity::boundMenuFunctionAvailable(const uint8_t function) const {
  switch (function) {
    case CrossPointSettings::LP_MENU_KOSYNC:
      return KOREADER_STORE.hasCredentials();
    case CrossPointSettings::LP_MENU_DICTIONARY:
      // No dictionary folder configured means the picker would open only to report it.
      return SETTINGS.dictionaryName[0] != '\0' && section != nullptr;
    case CrossPointSettings::LP_MENU_GRAB_QUOTE:
      // The picker is handed this section and outlives the call, so a missing one is fatal.
      return section != nullptr;
    case CrossPointSettings::LP_MENU_GO_TO_PARAGRAPH:
      // The number to type is the one the marks print; with numbering off there is none.
      return prefs_.paragraphNumbering != 0;
    case CrossPointSettings::LP_MENU_FOOTNOTES:
      return !currentPageFootnotes.empty();
    case CrossPointSettings::LP_MENU_SELECT_CHAPTER:
    case CrossPointSettings::LP_MENU_GO_TO_PERCENT:
      return epub != nullptr;
    case CrossPointSettings::LP_MENU_POPUP:
      // An empty pop-up is a dead press; Pop-up Items has not been filled in yet.
      return SETTINGS.popupItemCount() > 0;
    case CrossPointSettings::LP_MENU_WALLPAPER_HOLD:
      // Same test the in-book menu uses to offer its own row: there must be a wallpaper
      // the lock screen actually showed, and it must still be on the card.
      return !APP_STATE.lastSleepWallpaperPath.empty() && Storage.exists(APP_STATE.lastSleepWallpaperPath.c_str());
    case CrossPointSettings::LP_MENU_BOOKMARK:
    case CrossPointSettings::LP_MENU_TEXT_SETTINGS:
    case CrossPointSettings::LP_MENU_READER_SETTINGS:
    case CrossPointSettings::LP_MENU_TOGGLE_STATUS_BAR:
      return true;
    case CrossPointSettings::LP_MENU_DISABLED:
    default:
      return false;
  }
}

bool EpubReaderActivity::runBoundMenuFunction(const uint8_t function) {
  // Every caller treats false as "the press was not consumed", so refusing here is what
  // keeps a bound-but-impossible action from swallowing the button.
  if (!boundMenuFunctionAvailable(function)) return false;

  switch (function) {
    case CrossPointSettings::LP_MENU_BOOKMARK:
      if (showBookmarkMessage) return false;
      addBookmark();
      showBookmarkMessage = true;
      bookmarkMessageTime = millis();
      requestUpdate();
      return true;
    case CrossPointSettings::LP_MENU_KOSYNC:
      // False when sync cannot run (no credentials stored); the caller then leaves the
      // Confirm release alone so the normal reader menu still opens.
      return launchKOReaderSync();
    case CrossPointSettings::LP_MENU_DICTIONARY:
      if (showDictionaryMessage) return false;
      openDictionaryWordSelect();
      return true;
    case CrossPointSettings::LP_MENU_GRAB_QUOTE:
      openQuoteGrab();
      return true;
    // The rest reuse the in-book menu's own entry points rather than repeating them, so a
    // binding and the matching menu row can never drift apart.
    case CrossPointSettings::LP_MENU_SELECT_CHAPTER:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER);
      return true;
    case CrossPointSettings::LP_MENU_GO_TO_PERCENT:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::GO_TO_PERCENT);
      return true;
    case CrossPointSettings::LP_MENU_GO_TO_PARAGRAPH:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::GO_TO_PARAGRAPH);
      return true;
    case CrossPointSettings::LP_MENU_FOOTNOTES:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::FOOTNOTES);
      return true;
    case CrossPointSettings::LP_MENU_TEXT_SETTINGS:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::TEXT_SETTINGS);
      return true;
    case CrossPointSettings::LP_MENU_READER_SETTINGS:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::READER_SETTINGS);
      return true;
    case CrossPointSettings::LP_MENU_TOGGLE_STATUS_BAR:
      // Not a menu row: the menu toggles this live and reports it through MenuResult, so
      // there is no MenuAction to reuse. sbOffBar is passed unchanged — this flips the
      // per-book bar only, never the global hidden-bar progress setting.
      applyStatusBar(prefs_.statusBarEnabled ? 0 : 1, SETTINGS.sbOffBar);
      return true;
    case CrossPointSettings::LP_MENU_POPUP:
      openQuickMenu();
      return true;
    case CrossPointSettings::LP_MENU_WALLPAPER_HOLD:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::WALLPAPER_HOLD);
      return true;
    case CrossPointSettings::LP_MENU_DISABLED:
    default:
      return false;
  }
}

void EpubReaderActivity::runPowerDoubleClick() {
  // The pop-up already owns the buttons; a second double click while it is up must not
  // rebuild it underneath itself.
  if (quickMenu.isActive()) return;

  const uint8_t function = SETTINGS.doubleClickPowerFunction;
  // Bound but impossible right now (no footnote on this page, numbering off, no KOReader
  // credentials). Saying so beats a button that silently does nothing.
  if (!runBoundMenuFunction(function)) {
    GUI.drawPopup(renderer, tr(STR_NOT_AVAILABLE));
    scheduleGhostCleanup();
    requestUpdate();
  }
}

void EpubReaderActivity::openQuickMenu() {
  const auto& functions = CrossPointSettings::POPUP_ITEM_FUNCTIONS;

  std::vector<std::string> labels;
  std::vector<bool> disabledRows;
  labels.reserve(CrossPointSettings::POPUP_ITEM_MAX);
  disabledRows.reserve(CrossPointSettings::POPUP_ITEM_MAX);
  quickMenuFunctions.clear();
  quickMenuFunctions.reserve(CrossPointSettings::POPUP_ITEM_MAX);

  for (const uint8_t function : functions) {
    if (!SETTINGS.isPopupItem(function)) continue;
    const bool available = boundMenuFunctionAvailable(function);
    // Fixed-width status column, and the pop-up is left-aligned, so ticking or losing a
    // footnote never shifts a label sideways.
    labels.push_back(std::string(available ? "    " : "[X] ") + I18N.get(boundMenuActionLabel(function)));
    disabledRows.push_back(!available);
    quickMenuFunctions.push_back(function);
  }

  if (quickMenuFunctions.empty()) return;

  quickMenu.showWithDisabled(StrId::STR_QUICK_MENU, labels, disabledRows, 0, true, [this](const int index) {
    if (index < 0 || index >= static_cast<int>(quickMenuFunctions.size())) return;
    const uint8_t function = quickMenuFunctions[index];
    // The pop-up has already closed itself by the time this runs, so the action draws
    // over a page the pop-up no longer owns.
    if (!runBoundMenuFunction(function)) {
      GUI.drawPopup(renderer, tr(STR_NOT_AVAILABLE));
      scheduleGhostCleanup();
    }
    requestUpdate();
  });
  requestUpdate();
}

void EpubReaderActivity::loop() {
  if (!epub) {
    // Should never happen
    finish();
    return;
  }

  // Must run before any early return below, so a genuine press is never missed. See
  // ReaderUtils::ButtonPressLatch: the reader acts on release, child screens close on
  // press, and an unpaired release used to throw the user out of the book.
  backLatch_.observe(mappedInput.wasPressed(MappedInputManager::Button::Back));
  confirmLatch_.observe(mappedInput.wasPressed(MappedInputManager::Button::Confirm));

  // The Quick Menu owns every button while it is up. Placed after the latches above so a
  // press it consumes is still paired with its release, and before every other handler so
  // no page turn or menu open leaks through from underneath it.
  if (quickMenu.handleInput(mappedInput, [this] { requestUpdate(); })) {
    // The pop-up painted a framed panel straight onto the page. The page repaint that
    // replaces it must be a cleanup refresh, or the frame ghosts through it.
    if (!quickMenu.isActive()) scheduleGhostCleanup();
    return;
  }

  // Idle glyph prewarm for the likely next page (currentPage + 1). The scan
  // pass draws nothing (FCM scan mode suppresses pixels), so the displayed
  // framebuffer is untouched; endScanAndPrewarm loads only glyphs not already
  // cached. Debounced past rapid page-flipping, one attempt per position, and
  // deferred while a render/build owns the CPU or the heap is at the render
  // floor. Cross-chapter prewarm is deliberately out of scope (next spine's
  // section isn't loaded).
  constexpr unsigned long IDLE_PREWARM_DEBOUNCE_MS = 400;
  if (section && !section->isBuilding() && !RenderLock::peek() && renderer.hasFrameBuffer() &&
      lastRenderCompleteMs != 0 && millis() - lastRenderCompleteMs > IDLE_PREWARM_DEBOUNCE_MS &&
      ESP.getFreeHeap() > RENDER_MIN_FREE_HEAP && ESP.getMaxAllocHeap() > BACKGROUND_BUILD_MIN_MAX_ALLOC &&
      (idlePrewarmSpine != currentSpineIndex || idlePrewarmPage != section->currentPage)) {
    RenderLock lock;  // the page table must not change under the scan
    // Re-check under the lock: peek() and acquisition are not atomic, so the render
    // task may have reset/replaced the section or moved the page in between.
    if (section && !section->isBuilding() &&
        (idlePrewarmSpine != currentSpineIndex || idlePrewarmPage != section->currentPage)) {
      idlePrewarmSpine = currentSpineIndex;
      idlePrewarmPage = section->currentPage;
      const int nextPage = section->currentPage + 1;
      if (nextPage < static_cast<int>(section->pageCount)) {
        if (const auto p = section->loadPage(nextPage)) {
          if (auto* fcm = renderer.getFontCacheManager()) {
            const auto t0 = millis();
            auto scope = fcm->createPrewarmScope();
            p->render(renderer, SETTINGS.getReaderFontId(prefs_), 0, 0);  // scan only, no pixels
            scope.endScanAndPrewarm();
            LOG_DBG("ERS", "Idle prewarm: page %d in %lums", nextPage, millis() - t0);
          }
        }
      }
    }
  }

  // Lazily resume a partial's extension build once the reader nears its watermark. Far from
  // it the rebuild is all cost (whole-chapter re-layout from page 0) and no benefit this
  // session, so reopening a partial deliberately does NOT start it (see the deferral in
  // render()); crossing this margin is the signal that the reader will actually need pages
  // past the watermark soon. Uses the last render's viewport so pagination matches the
  // partial being extended.
  if (section && !section->isBuilding() && section->isPartial() && !RenderLock::peek() && buildViewportWidth > 0 &&
      !partialRebuildStartFailed &&
      section->currentPage + PARTIAL_REBUILD_START_MARGIN >= static_cast<int>(section->pageCount)) {
    RenderLock lock;
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
  if (section && section->isBuilding() && !RenderLock::peek() &&
      (section->isPartial() || static_cast<int>(section->pageCount) < section->currentPage + BUILD_WINDOW_AHEAD) &&
      buildTickHeapGate()) {
    RenderLock lock;
    // Re-check under the lock: render() (which also holds the RenderLock) may have finalized the
    // build between the outer isBuilding() check and acquiring the lock here, in which case
    // buildSomeMore() would fail and wrongly reset the section. The heap gate must be re-read
    // too: a render that won the lock race can expand retained glyph buffers, invalidating the
    // pre-lock heap reading. cppcheck can't see the cross-task mutation, so it flags this as
    // always true.
    // cppcheck-suppress knownConditionTrueFalse
    if (section->isBuilding() && buildTickHeapGate()) {
      if (!section->buildSomeMore(BACKGROUND_BUILD_PAGES_PER_TICK)) {
        LOG_ERR("ERS", "Background section build failed");
        section.reset();
        requestUpdate();
      } else if (section->isBuildComplete() && applyDeferredReposition()) {
        // The chapter re-paginated since the saved progress (settings changed): we now know the
        // real page count, so re-render at the remapped page. No-op for an unchanged resume.
        requestUpdate();
      }
    }
  }

  // End-of-Book screen reached (currentSpineIndex == spine count) means the book is
  // finished. Two independent finished-book features key off this same condition.
  const bool atEndOfBook = currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount();

  // Drop this book from the Recent Books list; if the reader then pages back into the book,
  // re-add it. So removal only sticks if the reader leaves while still on the End-of-Book
  // screen. Acts only on the transition (guarded by recentsEntryRemoved) — no per-frame writes.
  if (SETTINGS.removeReadBooksFromRecents) {
    if (atEndOfBook && !recentsEntryRemoved) {
      // Only treat the book as "removed by us" if it was actually in the list, so the
      // re-add branch below doesn't insert a book the feature never removed.
      recentsEntryRemoved = RECENT_BOOKS.removeByPath(epub->getPath());
    } else if (!atEndOfBook && recentsEntryRemoved) {
      // Re-add (goes to front of the list via addBook — accepted ordering side effect).
      RECENT_BOOKS.addBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), epub->getThumbBmpPath());
      recentsEntryRemoved = false;
    }
  }

  // Arm the move here so ANY exit path (Back, Home, file browser) relocates the book into
  // /Read/ in onExit(); paging back off the end screen disarms it (book not actually
  // finished). If removeReadBooksFromRecents also fired, RecentBooksStore::updatePath in the
  // move path becomes a safe no-op since the entry was already removed.
  if (atEndOfBook) {
    pendingReadFolderMove = SETTINGS.moveFinishedToReadFolder && !isInReadFolder(epub->getPath());
  } else {
    pendingReadFolderMove = false;
  }

  if (automaticPageTurnActive) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      automaticPageTurnActive = false;
      // updates chapter title space to indicate page turn disabled
      requestUpdate();
      return;
    }

    if (!section) {
      requestUpdate();
      return;
    }

    // Skips page turn if renderingMutex is busy
    if (RenderLock::peek()) {
      lastPageTurnTime = millis();
      return;
    }

    if ((millis() - lastPageTurnTime) >= pageTurnDuration) {
      pageTurn(true);
      return;
    }
  }

  if (showBookmarkMessage && (millis() - bookmarkMessageTime) >= ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS) {
    showBookmarkMessage = false;
    // The banner sat over the page; clear its residue on the paint that removes it.
    scheduleGhostCleanup();
    requestUpdate();
  }

  if (showDictionaryMessage && (millis() - dictionaryMessageTime) >= ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS) {
    showDictionaryMessage = false;
    scheduleGhostCleanup();
    requestUpdate();
  }

  // While the end screen suggestion menu is showing it owns Confirm/Back/navigation
  // input. Anything it doesn't handle (e.g. long-press Back to the file browser) falls
  // through to the regular handlers below; page turns are absorbed by the end-of-book
  // block. A Confirm release after a long-press function (bookmark/sync) fired is left
  // to the regular Confirm handler below, which consumes it via ignoreNextConfirmRelease.
  if (atEndOfBook && endOfBookOptions.menuActive() &&
      !(ignoreNextConfirmRelease && mappedInput.wasReleased(MappedInputManager::Button::Confirm))) {
    std::string openPath;
    switch (endOfBookOptions.handleMenuInput(mappedInput, &openPath)) {
      case EndOfBookOptions::Action::OpenBook:
        activityManager.goToReader(openPath);
        return;
      case EndOfBookOptions::Action::GoHome:
        onGoHome();
        return;
      case EndOfBookOptions::Action::LastPage:
        currentSpineIndex = std::max(epub->getSpineItemsCount() - 1, 0);
        nextPageNumber = 0;
        pendingPageJump = std::numeric_limits<uint16_t>::max();
        requestUpdate();
        return;
      case EndOfBookOptions::Action::Redraw:
        requestUpdate();
        return;
      case EndOfBookOptions::Action::None:
        break;
    }
  }

  // Enter reader menu activity on short-press Confirm or a downward swipe from the top edge. A long-press
  // that fired a bound function (bookmark or KOReader sync) sets ignoreNextConfirmRelease so the release
  // following the hold does not also open the menu.
  if (confirmLatch_.release(mappedInput.wasReleased(MappedInputManager::Button::Confirm))) {
    if (ignoreNextConfirmRelease) {
      ignoreNextConfirmRelease = false;
    } else {
      openReaderMenu();
    }
  }

  // Long-press Confirm runs the user-selected function (SETTINGS.longPressMenuFunction).
  //
  // confirmLatch_.seen gates this for the same reason it gates the release above: the
  // hold must have STARTED here. Popups act on the press (OptionPopup, and so every
  // ConfirmationActivity), so confirming one hands this activity a button that is still
  // held, with a held-time already past the threshold. Without this check, confirming
  // "Delete wallpaper?" from the reader menu ran the bound long-press function the moment
  // the reader resumed -- which, on the default KOSync binding, silently started WiFi and
  // a sync the user never asked for. An inherited hold is now ignored until the button is
  // released and pressed again inside the reader.
  if (confirmLatch_.seen && mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
    // KOSync asks for a longer hold (~1s) than the others (~0.4s): it starts WiFi, so an
    // accidental brush must not trigger it. A function that declines (KOSync without
    // credentials) leaves the release alone, so the normal reader menu still opens.
    const unsigned long holdThreshold = (SETTINGS.longPressMenuFunction == CrossPointSettings::LP_MENU_KOSYNC)
                                            ? ReaderUtils::GO_HOME_MS
                                            : ReaderUtils::BOOKMARK_HOLD_MS;
    if (SETTINGS.longPressMenuFunction != CrossPointSettings::LP_MENU_DISABLED &&
        mappedInput.getHeldTime() >= holdThreshold && runBoundMenuFunction(SETTINGS.longPressMenuFunction)) {
      ignoreNextConfirmRelease = true;  // suppress the menu on the release that follows
      return;
    }
  }

  // Short press Back restores position when viewing a footnote (takes priority over navigation)
  if (footnoteDepth > 0 && mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_BACK_OR_HOME_MS) {
    restoreSavedPosition();
    return;
  }

  if (ReaderUtils::handleBackNavigation(mappedInput, activityManager, epub ? epub->getPath().c_str() : "",
                                        {this, [](void* ctx) { static_cast<EpubReaderActivity*>(ctx)->onGoHome(); }},
                                        backLatch_)) {
    return;
  }

  // auto [prevTriggered, nextTriggered] = ReaderUtils::detectPageTurn(mappedInput);

  // Handle short power button press for footnotes
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FOOTNOTES &&
      mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      !mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (footnoteDepth > 0) {
      restoreSavedPosition();
    } else {
      if (currentPageFootnotes.size() == 1) {
        navigateToHref(currentPageFootnotes[0].href, true);
      } else if (currentPageFootnotes.size() > 1) {
        startActivityForResult(
            std::make_unique<EpubReaderFootnotesActivity>(renderer, mappedInput, currentPageFootnotes),
            [this](const ActivityResult& result) {
              if (!result.isCancelled) {
                const auto& footnoteResult = std::get<FootnoteResult>(result.data);
                navigateToHref(footnoteResult.href, true);
              }
              requestUpdate();
            });
      }
    }
    return;
  }

  auto [prevTriggered, nextTriggered] = ReaderUtils::detectPageTurn(mappedInput);
  if (!prevTriggered && !nextTriggered) {
    return;
  }

  // At end of the book with no suggestion menu, forward button goes home and back
  // button returns to last page
  if (currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount()) {
    if (endOfBookOptions.menuActive()) {
      // Selection movement was handled above; absorb leftover page-turn triggers so
      // e.g. "previous" at the top of the list doesn't jump back into the book
      return;
    }
    if (nextTriggered) {
      onGoHome();
    } else {
      currentSpineIndex = epub->getSpineItemsCount() - 1;
      nextPageNumber = 0;
      pendingPageJump = std::numeric_limits<uint16_t>::max();
      requestUpdate();
    }
    return;
  }

  const unsigned long heldMs = mappedInput.getHeldTime();
  const bool longPress = heldMs > ReaderUtils::SKIP_HOLD_MS;

  // Don't skip chapter after screenshot
  if (gpio.wasReleased(HalGPIO::BTN_POWER) && gpio.wasReleased(HalGPIO::BTN_DOWN)) {
    return;
  }

  if (longPress && SETTINGS.longPressButtonBehavior == SETTINGS.CHAPTER_SKIP) {
    if (!nextTriggered && section && section->currentPage > 0) {
      section->currentPage = 0;
      requestUpdate();
      return;
    }

    // We don't want to delete the section mid-render, so grab the semaphore
    {
      RenderLock lock(*this);
      nextPageNumber = 0;
      if (nextTriggered) {
        currentSpineIndex++;
      } else if (currentSpineIndex > 0) {
        currentSpineIndex--;
      }
      section.reset();
    }
    requestUpdate();
    return;
  }

  if (longPress && SETTINGS.longPressButtonBehavior == SETTINGS.ORIENTATION_CHANGE) {
    const uint8_t newOrientation =
        nextTriggered ? (SETTINGS.orientation - 1 + SETTINGS.ORIENTATION_COUNT) % SETTINGS.ORIENTATION_COUNT
                      : (SETTINGS.orientation + 1) % SETTINGS.ORIENTATION_COUNT;
    applyOrientation(newOrientation);
    requestUpdate();
    return;
  }

  // No current section, attempt to rerender the book
  if (!section) {
    requestUpdate();
    return;
  }

  if (prevTriggered) {
    pageTurn(false);
  } else {
    pageTurn(true);
  }
}

// Translate an absolute percent into a spine index plus a normalized position
// within that spine so we can jump after the section is loaded.
void EpubReaderActivity::jumpToPercent(int percent) {
  if (!epub) {
    return;
  }

  const size_t bookSize = epub->getBookSize();
  if (bookSize == 0) {
    return;
  }

  // Normalize input to 0-100 to avoid invalid jumps.
  percent = clampPercent(percent);

  // Convert percent into a byte-like absolute position across the spine sizes.
  // Use an overflow-safe computation: (bookSize / 100) * percent + (bookSize % 100) * percent / 100
  size_t targetSize =
      (bookSize / 100) * static_cast<size_t>(percent) + (bookSize % 100) * static_cast<size_t>(percent) / 100;
  if (percent >= 100) {
    // Ensure the final percent lands inside the last spine item.
    targetSize = bookSize - 1;
  }

  const int spineCount = epub->getSpineItemsCount();
  if (spineCount == 0) {
    return;
  }

  int targetSpineIndex = spineCount - 1;
  size_t prevCumulative = 0;

  for (int i = 0; i < spineCount; i++) {
    const size_t cumulative = epub->getCumulativeSpineItemSize(i);
    if (targetSize <= cumulative) {
      // Found the spine item containing the absolute position.
      targetSpineIndex = i;
      prevCumulative = (i > 0) ? epub->getCumulativeSpineItemSize(i - 1) : 0;
      break;
    }
  }

  const size_t cumulative = epub->getCumulativeSpineItemSize(targetSpineIndex);
  const size_t spineSize = (cumulative > prevCumulative) ? (cumulative - prevCumulative) : 0;
  // Store a normalized position within the spine so it can be applied once loaded.
  pendingSpineProgress =
      (spineSize == 0) ? 0.0f : static_cast<float>(targetSize - prevCumulative) / static_cast<float>(spineSize);
  if (pendingSpineProgress < 0.0f) {
    pendingSpineProgress = 0.0f;
  } else if (pendingSpineProgress > 1.0f) {
    pendingSpineProgress = 1.0f;
  }

  // Reset state so render() reloads and repositions on the target spine.
  {
    RenderLock lock(*this);
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    pendingPercentJump = true;
    section.reset();
  }
}

void EpubReaderActivity::onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action) {
  auto progressChangeResultHandler = [this](const ActivityResult& result) {
    loadCachedBookmarks();
    if (!result.isCancelled) {
      const auto& sync = std::get<ProgressChangeResult>(result.data);

      // Preferred path: a bookmark carrying an exact content offset. It is immune to
      // re-pagination, so resolve by content instead of trusting a page number saved under
      // possibly-different settings.
      if (sync.hasVisibleTextOffset && sync.spineIndex >= 0 && sync.spineIndex < epub->getSpineItemsCount()) {
        RenderLock lock(*this);
        if (section && currentSpineIndex == sync.spineIndex) {
          // Already in this chapter and laid out: resolve straight away, no reload.
          const auto page = section->getPageForVisibleTextOffset(sync.visibleTextOffset);
          section->currentPage = page.value_or(std::max(0, sync.page));
        } else {
          // Different chapter: reload and let render() build to the offset before drawing.
          currentSpineIndex = sync.spineIndex;
          pendingOffsetJump = sync.visibleTextOffset;
          nextPageNumber = std::max(0, sync.page);  // hint until the offset resolves
          section.reset();
        }
        return;
      }

      int targetSpineIndex = sync.spineIndex;
      int targetPage = sync.page;
      const int activeTotalPages = section ? section->estimatedTotalPages() : 0;
      const bool cachedPageMatchesActiveSection = section && sync.totalPages > 0 &&
                                                  currentSpineIndex == sync.spineIndex && sync.page >= 0 &&
                                                  sync.page < sync.totalPages && activeTotalPages == sync.totalPages;

      if (!cachedPageMatchesActiveSection && sync.hasSavedProgress) {
        const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
        CrossPointPosition fallback =
            ProgressMapper::toCrossPoint(epub, {sync.xpath, sync.percentage}, renderer, currentSpineIndex, totalPages);
        targetSpineIndex = fallback.spineIndex;
        targetPage = fallback.pageNumber;
      }

      if (currentSpineIndex != targetSpineIndex) {
        RenderLock lock(*this);
        currentSpineIndex = targetSpineIndex;
        nextPageNumber = targetPage;
        section.reset();
      } else if (section && section->currentPage != targetPage) {
        RenderLock lock(*this);
        const int clampedTargetPage = std::max(0, targetPage);
        section->currentPage = clampedTargetPage;
      } else if (!section) {
        nextPageNumber = targetPage;
      }
    }
  };

  switch (action) {
    case EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER: {
      const int spineIdx = currentSpineIndex;
      const std::string path = epub->getPath();
      startActivityForResult(
          std::make_unique<EpubReaderChapterSelectionActivity>(renderer, mappedInput, epub, path, spineIdx),
          [this](const ActivityResult& result) {
            if (!result.isCancelled) {
              const auto& chapterResult = std::get<ChapterResult>(result.data);
              RenderLock lock(*this);

              currentSpineIndex = chapterResult.spineIndex;

              // If anchor is not empty, it will be used later to calculate the page number.
              pendingAnchor = chapterResult.anchor;

              // Otherwise page 0 will be used.
              nextPageNumber = 0;

              section.reset();
            }
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::FOOTNOTES: {
      startActivityForResult(std::make_unique<EpubReaderFootnotesActivity>(renderer, mappedInput, currentPageFootnotes),
                             [this](const ActivityResult& result) {
                               if (!result.isCancelled) {
                                 const auto& footnoteResult = std::get<FootnoteResult>(result.data);
                                 navigateToHref(footnoteResult.href, true);
                               }
                               requestUpdate();
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::TEXT_SETTINGS: {
      startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                                    TextSettingsActivity::Tab::Family),
                             [this](const ActivityResult&) {
                               // TextSettingsActivity saves on each change; no save needed here.
                               // Font/size/spacing/margin changes invalidate the current
                               // layout: preserve position and force a re-layout, mirroring
                               // applyOrientation()'s reflow.
                               RenderLock lock(*this);
                               // Same relayout as a per-book prefs change, so it takes the
                               // same path: the paragraph anchor is what puts the reader
                               // back, and the quote underline memo has to go with it.
                               dropSectionForRelayout();
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_TO_PERCENT: {
      float bookProgress = 0.0f;
      if (epub && epub->getBookSize() > 0 && section && section->pageCount > 0) {
        const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
        bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
      }
      const int initialPercent = clampPercent(static_cast<int>(bookProgress + 0.5f));
      startActivityForResult(
          std::make_unique<EpubReaderPercentSelectionActivity>(renderer, mappedInput, initialPercent),
          [this](const ActivityResult& result) {
            if (!result.isCancelled) {
              jumpToPercent(std::get<PercentResult>(result.data).percent);
            }
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DICTIONARY: {
      openDictionaryWordSelect();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GRAB_QUOTE: {
      openQuoteGrab();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::VIEW_QUOTES: {
      // The row only appears when the sidecar exists, so the viewer opens straight
      // onto the file Grab Quote wrote.
      startActivityForResult(
          std::make_unique<QuotesViewerActivity>(renderer, mappedInput, quote_text::quotesFilePathFor(epub->getPath())),
          [this](const ActivityResult&) {
            loadQuoteAnchors();  // a delete in there rewrites the sidecar
            requestUpdate();
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::READING_STATS: {
      openReadingStats();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::WALLPAPER_FAVORITE: {
      // Favouriting must not stand between the press and the page. The rename plus the
      // APP_STATE save are card work worth hundreds of milliseconds, and none of it is
      // needed to draw anything, so it is queued and the page repaint below starts at
      // once — the panel refresh and the card work then overlap instead of queueing.
      //
      // No popup. A popup paints and refreshes on its own, and then the page repaint
      // refreshes again: two waits for one press, which is the cost this change exists
      // to remove. The Favorite / Unfavorite row label carries the result instead, and
      // it is already correct because APP_STATE moves to the new name right here.
      //
      // Moving APP_STATE now rather than after the rename is what lets the UI move on.
      // It is a promise, and DeferredFavorite takes it back if the rename fails.
      const std::string lastPath = APP_STATE.lastSleepWallpaperPath;
      if (lastPath.empty()) break;
      const bool makeFavorite = !FavoriteImage::isFavoritePath(lastPath);
      const std::string newPath = FavoriteImage::favoritePathFor(lastPath, makeFavorite);
      APP_STATE.lastSleepWallpaperPath = newPath;
      if (!DeferredFavorite::request(lastPath, newPath)) {
        // Could not queue it (the worker would not start, or the queue is jammed). Do it
        // here rather than drop the press, and report failure the way the old path did.
        APP_STATE.lastSleepWallpaperPath = lastPath;
        if (FavoriteImage::setFavorite(lastPath, makeFavorite, nullptr) != FavoriteImage::SetFavoriteResult::Success) {
          GUI.drawPopup(renderer, tr(STR_FAVORITE_FAILED));
          scheduleGhostCleanup();
        }
      }
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::WALLPAPER_HOLD: {
      SETTINGS.wallpaperRotationPaused = SETTINGS.wallpaperRotationPaused ? 0 : 1;
      SETTINGS.saveToFile();
      GUI.drawPopup(renderer, SETTINGS.wallpaperRotationPaused ? tr(STR_ROTATION_PAUSED) : tr(STR_ROTATION_RESUMED));
      scheduleGhostCleanup();
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::WALLPAPER_PAUSE: {
      // A queued favourite rename may still own this file's name on the card
      // (renames run at flush points, not at the press). Land it first so the
      // move below acts on the name APP_STATE promises. Rare action, and it does
      // a synchronous rename itself, so the wait is in character here.
      DeferredFavorite::waitForIdle(15000);
      DeferredFavorite::reconcile();
      const std::string lastPath = APP_STATE.lastSleepWallpaperPath;
      if (lastPath.empty()) break;
      const auto moved = crosspoint::sleep::toggleSleepPause(lastPath);
      if (!moved.ok) {
        GUI.drawPopup(renderer, tr(STR_MOVE_FAILED));
        scheduleGhostCleanup();
      } else {
        GUI.drawPopup(renderer, moved.toPause ? tr(STR_WALLPAPER_PAUSED) : tr(STR_WALLPAPER_UNPAUSED));
        scheduleGhostCleanup();
      }
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::WALLPAPER_DELETE: {
      // Same rule as WALLPAPER_PAUSE: land any queued rename so the delete hits
      // the file under its current on-card name.
      DeferredFavorite::waitForIdle(15000);
      DeferredFavorite::reconcile();
      const std::string lastPath = APP_STATE.lastSleepWallpaperPath;
      if (lastPath.empty()) break;
      // Deleting a file is not undoable, so it asks first — unlike favourite and pause,
      // which are both a rename away from being put back.
      startActivityForResult(
          std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE) + std::string("? "),
                                                 FavoriteImage::displayNameForPath(lastPath)),
          [this, lastPath](const ActivityResult& res) {
            if (res.isCancelled) {
              requestUpdate();
              return;
            }
            if (!Storage.remove(lastPath.c_str())) {
              GUI.drawPopup(renderer, tr(STR_DELETE_FAILED));
              scheduleGhostCleanup();
              requestUpdate();
              return;
            }
            // The wake path re-renders the last wallpaper to composite the unlock
            // banners over it; a dead path there sends the next wake to the boot logo.
            FavoriteImage::removePathReferences(lastPath);
            // Holding a wallpaper that no longer exists would freeze the rotation on
            // nothing, so deleting the held one resumes it.
            if (SETTINGS.wallpaperRotationPaused) {
              SETTINGS.wallpaperRotationPaused = 0;
              SETTINGS.saveToFile();
            }
            GUI.drawPopup(renderer, tr(STR_DONE));
            scheduleGhostCleanup();
            requestUpdate();
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::BOOK_INFO: {
      // The screen itself reads nothing: hand it the metadata already in memory and
      // a thumbnail path, rendering one first if this book has never needed a cover.
      constexpr int kInfoCoverHeight = 360;
      std::string coverPath;
      if (epub->generateThumbBmp(kInfoCoverHeight)) {
        const std::string path = epub->getThumbBmpPath(kInfoCoverHeight);
        if (Storage.exists(path.c_str())) coverPath = path;
      }
      startActivityForResult(
          std::make_unique<BookInfoActivity>(renderer, mappedInput, epub->getTitle(), epub->getAuthor(),
                                             epub->getLanguage(), epub->getDescription(), coverPath),
          [this](const ActivityResult&) { requestUpdate(); });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_TO_PARAGRAPH: {
      // Ask for a paragraph number, then jump to it (the number shown by the marks).
      startActivityForResult(
          std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, std::string(tr(STR_GO_TO_PARAGRAPH)),
                                                  std::string(), 6, InputType::Text),
          [this](const ActivityResult& result) {
            if (!result.isCancelled) {
              if (const auto* kr = std::get_if<KeyboardResult>(&result.data)) {
                const int n = atoi(kr->text.c_str());
                if (n >= 1) {
                  jumpToParagraph(n);
                  return;
                }
              }
            }
            requestUpdate();
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DISPLAY_QR: {
      if (section && section->currentPage >= 0 && section->currentPage < section->pageCount) {
        std::string fullText = section->getTextFromSectionFile();
        if (!fullText.empty()) {
          startActivityForResult(std::make_unique<QrDisplayActivity>(renderer, mappedInput, fullText),
                                 [this](const ActivityResult& result) {});
          break;
        }
      }
      // If no text or page loading failed, just close menu
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::STEAL_LOOK: {
      // Pick another book that has a custom look and copy its reader settings onto
      // this one. A one-time snapshot, not a link: changing that book later does not
      // change this one. A cancelled pick changes nothing.
      startActivityForResult(makeUniqueNoThrow<StealLookActivity>(renderer, mappedInput, epub->getPath()),
                             [this](const ActivityResult& res) {
                               if (res.isCancelled) {
                                 requestUpdate();
                                 return;
                               }
                               if (const auto* fp = std::get_if<FilePathResult>(&res.data)) {
                                 applyStolenLook(fp->path);
                               } else {
                                 requestUpdate();
                               }
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::READING_THEMES: {
      // The themes screen edits the store itself; only "Apply" comes back, as the index
      // of the theme to adopt. Cancelling, renaming or deleting changes nothing here.
      startActivityForResult(makeUniqueNoThrow<ReaderPresetsActivity>(renderer, mappedInput, prefs_),
                             [this](const ActivityResult& res) {
                               const auto* pr = res.isCancelled ? nullptr : std::get_if<PresetResult>(&res.data);
                               const ReaderPreset* preset = pr ? READER_PRESETS.get(pr->index) : nullptr;
                               if (!preset) {
                                 requestUpdate();
                                 return;
                               }
                               const ReaderPrefs stored = preset->prefs;
                               const ReaderPrefs adopted = applyReaderPrefsFrom(stored);
                               // A theme naming an SD font that has left the card comes back
                               // corrected. Write the correction to the card so the theme is
                               // repaired once, instead of falling back on every apply.
                               if (std::memcmp(&adopted, &stored, sizeof(ReaderPrefs)) != 0) {
                                 READER_PRESETS.overwrite(pr->index, adopted);
                               }
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::REMOVE_FROM_RECENTS: {
      // The file move and the list edit both happen in onExit, where the Epub is
      // already released — renaming a book with an open handle is what the /read and
      // /recents filing carefully avoids, and this is the same move in reverse.
      pendingRemoveFromRecents = true;
      onGoHome();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::DELETE_BOOK: {
      if (!epub) break;
      const std::string path = epub->getPath();
      const std::string name = path.substr(path.rfind('/') + 1);
      // Erasing a book file cannot be undone, so it asks first. The deletion itself
      // waits for onExit, where the Epub is already released — the same ordering the
      // filing move uses, for the same reason.
      startActivityForResult(
          std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_BOOK) + std::string("?"), name),
          [this](const ActivityResult& res) {
            if (res.isCancelled) {
              requestUpdate();
              return;
            }
            pendingDeleteBook = true;
            onGoHome();
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_HOME: {
      onGoHome();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::DELETE_CACHE: {
      {
        RenderLock lock(*this);
        if (epub && section) {
          uint16_t backupSpine = currentSpineIndex;
          uint16_t backupPage = section->currentPage;
          uint16_t backupPageCount = section->pageCount;
          section.reset();
          epub->clearCache();
          epub->setupCacheDir();
          if (!saveProgress(backupSpine, backupPage, backupPageCount)) {
            LOG_ERR("ERS", "Failed to save progress before cache clear");
          }
        }
      }
      onGoHome();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::SCREENSHOT: {
      {
        RenderLock lock(*this);
        pendingScreenshot = true;
      }
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SYNC: {
      launchKOReaderSync();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::BOOKMARKS: {
      startActivityForResult(
          std::make_unique<EpubReaderBookmarksActivity>(renderer, mappedInput, epub, epub->getPath()),
          progressChangeResultHandler);
      break;
    }
    case EpubReaderMenuActivity::MenuAction::TOGGLE_BOOKMARK: {
      addBookmark();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::READER_SETTINGS: {
      // Overlay this book's values onto the live settings so the existing text
      // settings screen edits them in place (guarded so global settings.json is
      // untouched); the result callback captures the edits into the book override.
      SETTINGS.beginReaderEditOverlay(prefs_, &EpubReaderActivity::readerEditSinkThunk, this);
      startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry()),
                             [this](const ActivityResult&) { applyReaderSettingsEdit(); });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::RESET_READER_SETTINGS: {
      resetReaderPrefsToGlobal();
      break;
    }
  }
}

bool EpubReaderActivity::launchKOReaderSync() {
  if (!KOREADER_STORE.hasCredentials()) return false;  // no-op: nothing to launch

  const int currentPage = section ? section->currentPage : nextPageNumber;
  const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
  std::optional<uint16_t> paragraphIndex;
  if (section && currentPage >= 0 && currentPage < section->pageCount) {
    const uint16_t paragraphPage =
        currentPage > 0 ? static_cast<uint16_t>(currentPage - 1) : static_cast<uint16_t>(currentPage);
    if (const auto pIdx = section->getParagraphIndexForPage(paragraphPage)) {
      paragraphIndex = *pIdx;
    }
  }

  // Pre-compute local KO position and chapter name while Epub is still in RAM.
  CrossPointPosition localPos = getCurrentPosition();
  SavedProgressPosition localKoPos = ProgressMapper::toSavedProgress(epub, localPos);
  const int tocIdx = epub->getTocIndexForSpineIndex(currentSpineIndex);
  std::string localChapterName = (tocIdx >= 0) ? epub->getTocItem(tocIdx).title : "";
  const std::string savedEpubPath = epub->getPath();

  // Persist current position so the reader resumes at the right page on return.
  // goToReader() depends on this file, so abort the sync if the write fails.
  if (!saveProgress(currentSpineIndex, currentPage, totalPages)) {
    LOG_ERR("KOSync", "Aborting sync because current progress could not be saved");
    pendingSyncSaveError = true;
    requestUpdate();
    return true;  // acted: surfaced a save error to the user
  }

  // Release Epub and Section to free ~65KB RAM for the TLS handshake.
  LOG_DBG("KOSync", "Releasing epub for sync (heap before: %u)", (unsigned)ESP.getFreeHeap());
  {
    RenderLock lock(*this);
    if (section) {
      nextPageNumber = section->currentPage;
    }
    // The image extractor holds a raw pointer into this epub (see onEnter);
    // clear it before the early release, mirroring onExit(), or a later image
    // render would call through a dangling context.
    ImageBlock::setExtractor(nullptr, nullptr);
    section.reset();
    epub.reset();
  }
  LOG_DBG("KOSync", "Epub released (heap after: %u)", (unsigned)ESP.getFreeHeap());

  activityManager.replaceActivity(std::make_unique<KOReaderSyncActivity>(
      renderer, mappedInput, savedEpubPath, currentSpineIndex, currentPage, totalPages, std::move(localKoPos),
      std::move(localChapterName), paragraphIndex));
  return true;  // acted: launched the sync activity
}

void EpubReaderActivity::applyOrientation(const uint8_t orientation) {
  // No-op if the selected orientation matches current settings.
  if (SETTINGS.orientation == orientation) {
    return;
  }

  // Preserve current reading position so we can restore after reflow.
  {
    RenderLock lock(*this);
    if (section) {
      rememberCurrentContentOffset();
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
      // Rotating re-flows the chapter for a different viewport, so the page number is
      // as unreliable here as it is after a font change; anchor on the paragraph too.
      captureOrdinalAnchor();
    }

    // Persist the selection so the reader keeps the new orientation on next launch.
    SETTINGS.orientation = orientation;
    SETTINGS.saveToFile();

    // Update renderer orientation to match the new logical coordinate system.
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

    // Reset section to force re-layout in the new orientation.
    section.reset();
    // Same page number, entirely different geometry: drop the remembered quote
    // underline segments so they are worked out again for the new viewport.
    underlineMemoSpine = -1;
    underlineMemoPage = -1;
    underlineMemo.clear();
  }
}

std::string EpubReaderActivity::readerOverridePath() const { return epub->getCachePath() + "/reader_override.bin"; }

void EpubReaderActivity::loadReaderPrefs() {
  prefsCustom_ = false;
  HalFile f;
  if (Storage.openFileForRead("ERS", readerOverridePath(), f)) {
    ReaderPrefs loaded;
    bool migrated = false;
    if (readReaderPrefs(f, loaded, &migrated)) {
      prefs_ = loaded;
      prefsCustom_ = true;
      // An upgraded sidecar is not applied here. The saved page number was produced by
      // the OLD layout, so the chapter is laid out that way first, the reading position
      // is read off it as a paragraph, and only then do the new defaults go in — see
      // applyPendingPrefsMigration(). Applying them now would land the reader on a page
      // number that no longer means anything.
      pendingPrefsMigration_ = migrated;
      LOG_DBG("ERS", "Loaded per-book reader override%s", migrated ? " (defaults upgrade pending)" : "");
      return;
    }
    LOG_ERR("ERS", "reader_override.bin present but unreadable; using global settings");
  }
  prefs_ = ReaderPrefs::fromGlobal();
}

bool EpubReaderActivity::writeReaderOverride(const ReaderPrefs& p) const {
  HalFile f;
  if (!Storage.openFileForWrite("ERS", readerOverridePath(), f)) {
    LOG_ERR("ERS", "Failed to open reader_override.bin for write");
    return false;
  }
  if (!writeReaderPrefs(f, p)) {
    LOG_ERR("ERS", "Short write to reader_override.bin");
    return false;
  }
  return true;
}

void EpubReaderActivity::reloadForReaderPrefsChange() {
  // Mirrors applyOrientation's reflow: keep the reading position, drop the section
  // so the next render rebuilds it with the new ReaderPrefs (CrossPoint's section
  // cache re-keys on the changed spec automatically — no cache machinery of ours).
  RenderLock lock(*this);
  dropSectionForRelayout();
}

void EpubReaderActivity::dropSectionForRelayout() {
  // Caller must already hold the render lock. RenderLock wraps xSemaphoreCreateMutex,
  // which is NOT recursive, so the render path takes this entry point instead of
  // reloadForReaderPrefsChange() — taking the mutex twice on one task blocks forever.
  if (section) {
    // The content offset is what makes the rebuild stop in the right place; the
    // paragraph below is what picks the landing page once it has.
    rememberCurrentContentOffset();
    cachedSpineIndex = currentSpineIndex;
    cachedChapterTotalPageCount = section->pageCount;
    nextPageNumber = section->currentPage;
    // nextPageNumber is kept only as the fallback: the new layout may have a different
    // page count entirely, so the paragraph below is what actually restores the place.
    captureOrdinalAnchor();
  }
  section.reset();
  // Quote underlines are memoised per (spine, page). A relayout keeps both of those
  // numbers but moves every word, so the remembered segments must go with the section.
  underlineMemoSpine = -1;
  underlineMemoPage = -1;
  underlineMemo.clear();
}

void EpubReaderActivity::readerEditSinkThunk(void* ctx, const ReaderPrefs& live) {
  static_cast<const EpubReaderActivity*>(ctx)->persistReaderSettingsEdit(live);
}

void EpubReaderActivity::persistReaderSettingsEdit(const ReaderPrefs& live) const {
  const ReaderOverrideDecision decision = decideReaderOverride(live, prefs_, prefsCustom_);
  switch (decision.action) {
    case ReaderOverrideAction::Write:
      writeReaderOverride(decision.prefs);
      break;
    case ReaderOverrideAction::Remove:
      Storage.remove(readerOverridePath().c_str());
      break;
    case ReaderOverrideAction::Keep:
      break;
  }
}

void EpubReaderActivity::applyReaderSettingsEdit() {
  ReaderPrefs edited = SETTINGS.endReaderEditOverlay();
  // paragraphNumbering and paperbackLook* are per-book in-menu toggles, not part of
  // the Reader Settings screen, so they are not in the overlay round-trip; carry the
  // book's values across so editing font/margins never resets them.
  edited.paragraphNumbering = prefs_.paragraphNumbering;
  edited.paragraphNumberSize = prefs_.paragraphNumberSize;
  edited.paperbackLookBody = prefs_.paperbackLookBody;
  edited.paperbackLookStatus = prefs_.paperbackLookStatus;
  if (std::memcmp(&edited, &prefs_, sizeof(ReaderPrefs)) == 0) {
    // Opened the settings screen but changed nothing — leave the book as it was.
    requestUpdate();
    return;
  }
  prefs_ = edited;
  prefsCustom_ = true;
  writeReaderOverride(prefs_);
  reloadForReaderPrefsChange();
  requestUpdate();
}

void EpubReaderActivity::resetReaderPrefsToGlobal() {
  Storage.remove(readerOverridePath().c_str());
  prefs_ = ReaderPrefs::fromGlobal();
  prefsCustom_ = false;
  reloadForReaderPrefsChange();
  requestUpdate();
}

void EpubReaderActivity::applyParagraphNumbering(const uint8_t mode, const uint8_t size) {
  if (mode == prefs_.paragraphNumbering && size == prefs_.paragraphNumberSize) return;
  prefs_.paragraphNumbering = mode;
  prefs_.paragraphNumberSize = size;
  prefsCustom_ = true;
  writeReaderOverride(prefs_);
  // No re-layout: the ordinals are already baked into the page cache; only whether
  // and how they are drawn changes, so a plain repaint suffices.
  requestUpdate();
}

void EpubReaderActivity::applyStatusBar(const uint8_t enabled, const uint8_t progressBar) {
  const bool barChanged = enabled != prefs_.statusBarEnabled;
  // Progress Bar is a global setting, not a per-book one, so it is compared against
  // SETTINGS rather than prefs_. The value-change guard is what keeps this off the
  // SPIFFS write path on every menu close.
  const bool progressBarChanged = progressBar != SETTINGS.sbOffBar;
  if (!barChanged && !progressBarChanged) return;
  if (barChanged) {
    prefs_.statusBarEnabled = enabled;
    prefsCustom_ = true;
    writeReaderOverride(prefs_);
  }
  if (progressBarChanged) {
    SETTINGS.sbOffBar = progressBar;
    SETTINGS.saveToFile();
  }
  // Unlike the toggles above these change the reserved top/bottom bands, so the viewport
  // changes and the chapter has to be laid out again. Same path as any margin change: the
  // reading position is held as a paragraph across the rebuild. Both are applied before
  // this single reload, so changing them together costs one repagination, not two.
  reloadForReaderPrefsChange();
}

void EpubReaderActivity::applyPaperbackLook(const uint8_t body, const uint8_t status) {
  if (body == prefs_.paperbackLookBody && status == prefs_.paperbackLookStatus) return;
  prefs_.paperbackLookBody = body;
  prefs_.paperbackLookStatus = status;
  prefsCustom_ = true;
  writeReaderOverride(prefs_);
  // Paperback only changes ink weight, not layout — no re-index, just repaint.
  requestUpdate();
}

std::string EpubReaderActivity::paragraphCountsPath() const { return epub->getCachePath() + "/paragraph_counts.bin"; }

uint32_t EpubReaderActivity::wholeBookParagraphBase(const int spineIndex) const {
  uint32_t base = 0;
  const int n = std::min(spineIndex, static_cast<int>(sectionParagraphCounts_.size()));
  for (int i = 0; i < n; i++) base += sectionParagraphCounts_[i];
  return base;
}

int EpubReaderActivity::findPageForOrdinal(Section& sec, const uint16_t ordinal, bool* const outFound) const {
  if (outFound) *outFound = false;
  const int pages = sec.pageCount;
  for (int p = 0; p < pages; p++) {
    auto page = sec.loadPage(p);
    if (!page) continue;
    for (const auto& el : page->elements) {
      if (el->getTag() != TAG_PageLine) continue;
      // Ordinals are contiguous and increase down the chapter, so the first page whose
      // lines reach the target holds that paragraph's first line.
      if (static_cast<const PageLine&>(*el).getParagraphOrdinal() >= ordinal) {
        if (outFound) *outFound = true;
        return p;
      }
    }
  }
  // Past the last paragraph laid out so far. On a finished chapter that means the last
  // page; on a chapter still building it means "not reached yet", which `outFound`
  // tells the caller apart so it can wait rather than land on the build watermark.
  return pages > 0 ? pages - 1 : 0;
}

std::optional<uint16_t> EpubReaderActivity::firstOrdinalOnPage(Section& sec, const int page) const {
  if (page < 0 || page >= sec.pageCount) return std::nullopt;
  const auto loaded = sec.loadPage(page);
  if (!loaded) return std::nullopt;
  // Only the FIRST line of a paragraph carries its ordinal; every other line is 0. So a
  // page that opens in the middle of a paragraph shows 0 until the next paragraph starts,
  // and the paragraph the page opened in is the one before that. That is the paragraph
  // the reader was looking at, which is what the anchor wants.
  bool openedMidParagraph = false;
  bool sawFirstLine = false;
  for (const auto& el : loaded->elements) {
    if (el->getTag() != TAG_PageLine) continue;
    const uint16_t ordinal = static_cast<const PageLine&>(*el).getParagraphOrdinal();
    if (!sawFirstLine) {
      sawFirstLine = true;
      openedMidParagraph = (ordinal == 0);
    }
    if (ordinal == 0) continue;
    if (!openedMidParagraph) return ordinal;  // page starts on a paragraph
    return (ordinal > 1) ? static_cast<uint16_t>(ordinal - 1) : ordinal;
  }
  // No paragraph starts on this page at all: it sits wholly inside one long paragraph,
  // and there is nothing here to name it by.
  return std::nullopt;
}

void EpubReaderActivity::captureOrdinalAnchor() {
  if (!section) return;
  // The anchor is the first paragraph that *starts* on this page. A paragraph running
  // across the page boundary from above resolves to its own first page, so the reader
  // can land slightly earlier than it was, never later. Earlier is the safe way to be
  // wrong: re-reading a line costs nothing, skipping one is a spoiler.
  if (const auto ordinal = firstOrdinalOnPage(*section, section->currentPage)) {
    pendingOrdinalAnchor_ = *ordinal;
  }
}

void EpubReaderActivity::jumpToParagraph(const int target) {
  if (!epub || target < 1) {
    requestUpdate();
    return;
  }

  int targetSpine = currentSpineIndex;
  uint16_t localOrdinal = static_cast<uint16_t>(target);

  if (prefs_.paragraphNumbering == CrossPointSettings::PARA_NUM_BOOK) {
    // Whole-book number: find the chapter whose [base, base+count) range contains it,
    // using the per-spine counts gathered as the book is read. The displayed number
    // uses the same base, so this matches what the reader sees. Counts for chapters not
    // yet read are 0 (skipped); a target past the counted range falls back to the
    // current chapter (best effort).
    const int spineCount = epub->getSpineItemsCount();
    const int known = std::min(spineCount, static_cast<int>(sectionParagraphCounts_.size()));
    bool found = false;
    for (int s = 0; s < known; s++) {
      const uint16_t count = sectionParagraphCounts_[s];
      if (count == 0) continue;
      const uint32_t base = wholeBookParagraphBase(s);
      if (static_cast<uint32_t>(target) <= base + count) {
        targetSpine = s;
        localOrdinal = static_cast<uint16_t>(target - base);
        found = true;
        break;
      }
    }
    if (!found) {
      const uint32_t base = wholeBookParagraphBase(currentSpineIndex);
      localOrdinal = (static_cast<uint32_t>(target) > base) ? static_cast<uint16_t>(target - base) : 1;
    }
  }
  if (localOrdinal < 1) localOrdinal = 1;

  if (targetSpine == currentSpineIndex && section) {
    // Same chapter, section already loaded — scan and move within it.
    const int page = findPageForOrdinal(*section, localOrdinal);
    RenderLock lock(*this);
    section->currentPage = page;
    nextPageNumber = page;
  } else {
    // Different chapter — switch spine and defer the page scan until it loads.
    RenderLock lock(*this);
    currentSpineIndex = targetSpine;
    nextPageNumber = 0;
    pendingParagraphScan_ = localOrdinal;
    section.reset();
  }
  requestUpdate();
}

void EpubReaderActivity::loadParagraphCounts() {
  const int spineCount = epub->getSpineItemsCount();
  sectionParagraphCounts_.assign(spineCount > 0 ? static_cast<size_t>(spineCount) : 0, 0);
  paragraphCountsDirty_ = false;
  if (spineCount <= 0) return;

  HalFile f;
  if (!Storage.openFileForRead("PNM", paragraphCountsPath(), f)) return;
  uint8_t version = 0;
  uint16_t storedSpineCount = 0;
  if (f.read(&version, 1) != 1 || version != PARAGRAPH_COUNTS_VERSION) return;
  if (f.read(reinterpret_cast<uint8_t*>(&storedSpineCount), 2) != 2) return;
  if (storedSpineCount != spineCount) return;  // spine changed (book edited) — rebuild counts
  for (int i = 0; i < spineCount; i++) {
    uint16_t c = 0;
    if (f.read(reinterpret_cast<uint8_t*>(&c), 2) != 2) {
      sectionParagraphCounts_.assign(static_cast<size_t>(spineCount), 0);  // truncated file — discard
      return;
    }
    sectionParagraphCounts_[i] = c;
  }
}

void EpubReaderActivity::saveParagraphCounts() {
  if (!paragraphCountsDirty_ || sectionParagraphCounts_.empty()) return;
  HalFile f;
  if (!Storage.openFileForWrite("PNM", paragraphCountsPath(), f)) return;
  const uint8_t version = PARAGRAPH_COUNTS_VERSION;
  const uint16_t spineCount = static_cast<uint16_t>(sectionParagraphCounts_.size());
  f.write(&version, 1);
  f.write(reinterpret_cast<const uint8_t*>(&spineCount), 2);
  for (const uint16_t c : sectionParagraphCounts_) {
    f.write(reinterpret_cast<const uint8_t*>(&c), 2);
  }
  paragraphCountsDirty_ = false;
}

// Copies another book's saved reader settings onto this one, once. Reads that book's
// reader_override.bin directly rather than going through its Epub, since the source
// book is not open. Writing our own override marks this book custom, so a later Reset
// still returns it to the global settings.
void EpubReaderActivity::applyStolenLook(const std::string& sourceCachePath) {
  ReaderPrefs stolen;
  HalFile f;
  if (!Storage.openFileForRead("ERS", sourceCachePath + "/reader_override.bin", f) || !readReaderPrefs(f, stolen)) {
    LOG_ERR("ERS", "Steal Look: source reader_override.bin missing or unreadable");
    requestUpdate();
    return;
  }
  applyReaderPrefsFrom(stolen);
}

// Adopt a whole set of reader settings from somewhere else — another book (Steal Look)
// or a saved preset. Shared so the two cannot drift apart in how they write, reload or
// resolve the font.
//
// Returns the prefs actually adopted, which differ from the argument when the SD font
// it names is no longer installed. The caller uses that to write the correction back to
// a stored preset, so a dead font name is repaired rather than re-applied forever.
ReaderPrefs EpubReaderActivity::applyReaderPrefsFrom(const ReaderPrefs& incoming) {
  ReaderPrefs next = incoming;

  // A preset saved months ago can name an SD font that has since left the card.
  // resolveFontId answers with whatever family is currently RESIDENT rather than the
  // one asked for, so an unchecked name lays the book out at the wrong font and size on
  // every open. Fall back to the built-in family instead, and report it.
  if (next.sdFontFamilyName[0] != '\0') {
    const auto& families = sdFontSystem.registry().getFamilies();
    const bool installed = std::any_of(families.begin(), families.end(), [&](const SdCardFontFamilyInfo& fam) {
      return fam.name == next.sdFontFamilyName;
    });
    if (!installed) {
      LOG_ERR("ERS", "Reader prefs name a missing SD font '%s' — falling back to the built-in family",
              next.sdFontFamilyName);
      std::memset(next.sdFontFamilyName, 0, sizeof(next.sdFontFamilyName));
      next.fontFamily = 0;  // CrossPointSettings::VOLLKORN
    }
  }

  if (std::memcmp(&next, &prefs_, sizeof(ReaderPrefs)) == 0) {
    requestUpdate();  // already identical — nothing to copy, and no re-layout to pay for
    return next;
  }
  prefs_ = next;
  prefsCustom_ = true;
  writeReaderOverride(prefs_);
  // Orientation is deliberately NOT part of ReaderPrefs here: rotating is a device
  // choice, not a book's look, so adopting a look never spins the screen.
  // The font LOAD is the authority, not resolveFontId: an SD family keeps exactly one
  // size resident, so the adopted size must be made resident before laying out with it.
  sdFontSystem.ensureLoadedFor(renderer, prefs_.sdFontFamilyName, prefs_.fontPointSize);
  reloadForReaderPrefsChange();
  requestUpdate();
  return next;
}

void EpubReaderActivity::drawParagraphNumbers(const Page& page, const int marginLeft, const int marginTop,
                                              const int fontId) {
  if (prefs_.paragraphNumbering == CrossPointSettings::PARA_NUM_OFF) return;
  const uint32_t base =
      (prefs_.paragraphNumbering == CrossPointSettings::PARA_NUM_BOOK) ? wholeBookParagraphBase(currentSpineIndex) : 0;
  constexpr int kGap = 5;  // px between the number and the first letter
  // Small and Double are two separate baked faces, not one face scaled: a bitmap font
  // only stays exact on whole multiples of its own cell, so the size is a choice between
  // pre-rendered grids rather than a scale factor applied here.
  const int numFontId =
      (prefs_.paragraphNumberSize == CrossPointSettings::PARA_NUM_SIZE_DOUBLE) ? PARA_NUM_2X_FONT_ID : PARA_NUM_FONT_ID;
  const int lineHeight = renderer.getLineHeight(fontId);
  const int numLineHeight = renderer.getLineHeight(numFontId);
  // Sit the number on the same optical line as the words, at any reading size. The rule
  // itself lives in ParagraphNumberLayout.h so the host tests can exercise it; the lookups
  // are hoisted here so they cost one glyph query per page rather than one per line.
  ParagraphNumberMetrics metrics;
  metrics.bodyAscender = renderer.getFontAscenderSize(fontId);
  metrics.bodyLineHeight = lineHeight;
  metrics.numAscender = renderer.getFontAscenderSize(numFontId);
  metrics.numLineHeight = numLineHeight;
  // 'x' because lowercase is the bulk of what a line of prose looks like; 'H' stands in
  // for a face without it, and the layout falls back to line-box centring without either.
  metrics.bodyInkTop = renderer.getGlyphInkTop(fontId, 'x');
  if (metrics.bodyInkTop <= 0) metrics.bodyInkTop = renderer.getGlyphInkTop(fontId, 'H');
  metrics.numInkTop = renderer.getGlyphInkTop(numFontId, '0');
  uint16_t pageMaxOrdinal = 0;
  for (const auto& el : page.elements) {
    if (el->getTag() != TAG_PageLine) continue;
    const auto& line = static_cast<const PageLine&>(*el);
    const uint16_t ord = line.getParagraphOrdinal();
    if (ord == 0) continue;  // not a paragraph's first line
    if (ord > pageMaxOrdinal) pageMaxOrdinal = ord;
    const auto& block = line.getBlock();
    if (!block || block->wordCount() == 0) continue;
    char buf[12];
    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(base + ord));
    const int numWidth = renderer.getTextWidth(numFontId, buf);
    // Right-align the number just left of the paragraph's first letter (wordXpos(0)
    // is non-zero for centered/justified/RTL lines, so it is the correct anchor).
    const int firstLetterX = marginLeft + line.xPos + block->wordXpos(0);
    const int x = firstLetterX - kGap - numWidth;
    if (x < 0) continue;  // no room in the margin — skip rather than clip into text
    metrics.lineTop = marginTop + line.yPos;
    const int y = paragraphNumberDrawY(metrics);
    renderer.drawText(numFontId, x, y, buf, true);
  }
  // Capture this chapter's running-max paragraph count so a later chapter's whole-book
  // base includes it. Finalizes as the book is read forward through each chapter.
  if (currentSpineIndex >= 0 && currentSpineIndex < static_cast<int>(sectionParagraphCounts_.size()) &&
      pageMaxOrdinal > sectionParagraphCounts_[currentSpineIndex]) {
    sectionParagraphCounts_[currentSpineIndex] = pageMaxOrdinal;
    paragraphCountsDirty_ = true;
  }
}

// Index the saved quotes: where each one sits, not what it says. Called on open
// and after anything rewrites the sidecar (a new grab, a delete in the viewer).
// The 24KB read is bounded by the writer's own cap and refused when the heap
// cannot spare it; on any refusal the book simply shows no underlines.
void EpubReaderActivity::loadQuoteAnchors() {
  quoteAnchors.clear();
  underlineMemoSpine = -1;
  underlineMemoPage = -1;
  underlineMemo.clear();
  if (!epub) return;

  const std::string path = quote_text::quotesFilePathFor(epub->getPath());
  if (!Storage.exists(path.c_str())) return;  // the common case: nothing to do

  HalFile file;
  if (!Storage.openFileForRead("ERS", path, file)) return;
  const size_t fileSize = file.size();
  if (fileSize == 0 || fileSize > quote_text::MAX_QUOTES_FILE_BYTES) return;
  if (ESP.getMaxAllocHeap() < fileSize + QUOTE_INDEX_HEAP_HEADROOM) {
    LOG_ERR("ERS", "Low heap for %u byte quotes file, skipping underlines", static_cast<unsigned>(fileSize));
    return;
  }
  std::string buf;
  buf.assign(fileSize, '\0');
  if (file.read(&buf[0], fileSize) != static_cast<int>(fileSize)) return;

  // Same record grammar the viewer parses: "[chapter @anchor]\nquote\n---\n\n".
  // Records with no anchor are skipped here — they stay bare in the text.
  quoteAnchors.reserve(MAX_QUOTE_ANCHORS);
  size_t pos = 0;
  while (pos < buf.size() && quoteAnchors.size() < MAX_QUOTE_ANCHORS) {
    while (pos < buf.size() && quote_text::isRecordGap(buf[pos])) ++pos;
    if (pos >= buf.size()) break;

    quote_text::QuoteAnchor anchor;
    if (buf[pos] == '[') {
      const auto close = buf.find(']', pos);
      if (close == std::string::npos) break;
      std::string chapter, token;
      quote_text::splitChapterAnchor(buf.substr(pos + 1, close - pos - 1), chapter, token);
      if (!token.empty()) quote_text::parseAnchorToken(token, anchor);
      pos = close + 1;
      while (pos < buf.size() && (buf[pos] == '\n' || buf[pos] == '\r')) ++pos;
    }

    const auto sep = buf.find("\n---", pos);
    const size_t textEnd = (sep == std::string::npos) ? buf.size() : sep;
    if (anchor.valid && textEnd > pos && (textEnd - pos) <= quote_underline::MAX_MATCH_BYTES) {
      QuoteAnchorRef ref;
      ref.textOffset = static_cast<uint32_t>(pos);
      ref.textLength = static_cast<uint16_t>(textEnd - pos);
      ref.spine = anchor.spine;
      ref.paragraph = anchor.paragraph;
      ref.wordHint = anchor.wordHint;
      quoteAnchors.push_back(ref);
    }
    if (sep == std::string::npos) break;
    pos = sep + 4;
  }
  LOG_DBG("ERS", "Quote anchors: %u", static_cast<unsigned>(quoteAnchors.size()));
}

// Draw the thin line under every saved quote visible on this page. Nothing is
// stored about where a quote sits on screen — the words are found again by their
// own text, so the line follows the passage through any font, margin or
// orientation change, and a passage that is not really here draws nothing.
void EpubReaderActivity::drawQuoteUnderlines(const Page& page, const int marginLeft, const int marginTop,
                                             const int fontId) {
  if (quoteAnchors.empty() || !section) return;

  const int lineHeight = renderer.getLineHeight(fontId);
  const int thickness = quote_underline::underlineThickness(lineHeight);
  const auto drawSegments = [&]() {
    for (const auto& seg : underlineMemo) {
      renderer.drawLine(seg.x1, seg.y, seg.x2, seg.y, thickness, true);
    }
  };

  // Same page as last render: the words have not moved, so replay the result.
  if (underlineMemoSpine == currentSpineIndex && underlineMemoPage == section->currentPage) {
    drawSegments();
    return;
  }
  underlineMemo.clear();
  underlineMemoSpine = currentSpineIndex;
  underlineMemoPage = section->currentPage;

  // Which paragraphs this page covers. A page that opens mid-paragraph carries no
  // ordinal until the next paragraph begins, so the one before that first ordinal
  // is the paragraph it opened in. A page wholly inside one paragraph has none at
  // all, and then the range check is skipped for every quote.
  uint16_t firstOrdinal = 0;
  uint16_t lastOrdinal = 0;
  for (const auto& el : page.elements) {
    if (el->getTag() != TAG_PageLine) continue;
    const uint16_t ord = static_cast<const PageLine&>(*el).getParagraphOrdinal();
    if (ord == 0) continue;
    if (firstOrdinal == 0) firstOrdinal = static_cast<uint16_t>(ord - 1);
    lastOrdinal = ord;
  }

  size_t candidates[quote_underline::MAX_QUOTES_PER_PAGE];
  size_t candidateCount = 0;
  for (size_t i = 0; i < quoteAnchors.size() && candidateCount < quote_underline::MAX_QUOTES_PER_PAGE; i++) {
    const auto& ref = quoteAnchors[i];
    if (static_cast<int>(ref.spine) != currentSpineIndex) continue;
    const bool ordinalKnown = ref.paragraph != 0 && firstOrdinal != 0;
    if (ordinalKnown) {
      // A quote starting after this page cannot show here at all. One starting before
      // it may still have its tail here, so look back a bounded number of paragraphs.
      if (ref.paragraph > lastOrdinal) continue;
      if (ref.paragraph + quote_underline::MAX_SPAN_PARAGRAPHS < firstOrdinal) continue;
    }
    candidates[candidateCount++] = i;
  }
  if (candidateCount == 0) return;  // nothing to look for: no tokens built, no SD read

  // Flatten the page's words: the text pointers the matcher walks, and the geometry
  // the line is drawn from. Pointers go straight into the TextBlock arena, which owns
  // NUL-terminated text and outlives this call, so no strings are copied.
  struct TokenBox {
    int16_t x;
    int16_t y;
    EpdFontFamily::Style style;
  };
  std::vector<const char*> texts;
  std::vector<TokenBox> boxes;
  texts.reserve(MAX_PAGE_TOKENS);
  boxes.reserve(MAX_PAGE_TOKENS);
  for (const auto& el : page.elements) {
    if (el->getTag() != TAG_PageLine) continue;
    const auto& line = static_cast<const PageLine&>(*el);
    const auto& block = line.getBlock();
    if (!block || !block->valid()) continue;
    for (uint16_t i = 0; i < block->wordCount() && texts.size() < MAX_PAGE_TOKENS; i++) {
      texts.push_back(block->wordText(i));
      boxes.push_back({static_cast<int16_t>(line.xPos + block->wordXpos(i) + marginLeft),
                       static_cast<int16_t>(line.yPos + marginTop), block->wordStyle(i)});
    }
  }
  if (texts.empty()) return;

  auto scratch = makeUniqueNoThrow<char[]>(quote_underline::MAX_MATCH_BYTES + 1);
  if (!scratch) {
    LOG_ERR("ERS", "OOM: quote underline scratch");
    return;
  }
  const std::string path = quote_text::quotesFilePathFor(epub->getPath());
  HalFile file;
  if (!Storage.openFileForRead("ERS", path, file)) return;

  const int ascender = renderer.getFontAscenderSize(fontId);
  for (size_t c = 0; c < candidateCount; c++) {
    const auto& ref = quoteAnchors[candidates[c]];
    if (!file.seek(ref.textOffset)) continue;
    const int read = file.read(scratch.get(), ref.textLength);
    if (read != static_cast<int>(ref.textLength)) continue;
    scratch[ref.textLength] = '\0';

    size_t first = 0, last = 0;
    if (!quote_underline::findQuoteRun(texts.data(), texts.size(), scratch.get(), ref.wordHint, first, last)) {
      // Not starting here. It may still be a quote grabbed on an earlier page whose
      // rest runs into this one, which always resumes at the page's first word.
      if (!quote_underline::findQuoteContinuation(texts.data(), texts.size(), scratch.get(), last)) continue;
      first = 0;
    }

    // One unbroken stroke per screen row: words that share a y are covered by a
    // single line from the first word's left edge to the last word's right edge,
    // so the spaces between them are underlined too.
    size_t i = first;
    while (i <= last && underlineMemo.size() < quote_underline::MAX_SEGMENTS_PER_PAGE) {
      const int16_t rowY = boxes[i].y;
      size_t rowEnd = i;
      while (rowEnd + 1 <= last && boxes[rowEnd + 1].y == rowY) rowEnd++;
      const int x2 = boxes[rowEnd].x + renderer.getTextAdvanceX(fontId, texts[rowEnd], boxes[rowEnd].style);
      underlineMemo.push_back(
          {boxes[i].x, static_cast<int16_t>(x2),
           static_cast<int16_t>(quote_underline::underlineY(rowY, ascender, lineHeight, thickness))});
      i = rowEnd + 1;
    }
  }
  drawSegments();
}

void EpubReaderActivity::toggleAutoPageTurn(const uint8_t selectedPageTurnOption) {
  if (selectedPageTurnOption == 0 || selectedPageTurnOption >= std::size(PAGE_TURN_RATES)) {
    automaticPageTurnActive = false;
    return;
  }

  lastPageTurnTime = millis();
  // calculates page turn duration by dividing by number of pages
  pageTurnDuration = (1UL * 60 * 1000) / PAGE_TURN_RATES[selectedPageTurnOption];
  automaticPageTurnActive = true;

  const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  // resets cached section so that space is reserved for auto page turn indicator when None or progress bar only
  if (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight()) {
    // Preserve current reading position so we can restore after reflow.
    RenderLock lock(*this);
    dropSectionForRelayout();
  }
}

void EpubReaderActivity::pageTurn(bool isForwardTurn) {
  if (statsTrackingActive) {
    // Only forward turns count as reading. A backward turn is re-reading, so the
    // page it leaves is closed out instead of being credited as progress.
    if (isForwardTurn) {
      statsSession.forwardTurn(millis());
      if (section && section->currentPage >= section->pageCount - 1 && !section->isBuilding() && epub &&
          currentSpineIndex >= epub->getSpineItemsCount() - 1) {
        const auto now = reading_stats::currentLocalDateTime();
        statsSession.markCompleted(now.valid ? now.dayIndex : 0);
      }
    } else {
      statsSession.pause(millis());
    }
  }
  if (isForwardTurn) {
    // Advance within the section while there are (or may still be) more pages: either a built
    // page ahead, or the section is still building (windowed), in which case more pages exist
    // beyond the current watermark and render()'s ensure-built pump will lay them out. Only when
    // the section is fully built AND we're on its last page do we move to the next spine -- using
    // the live pageCount alone would mistake the build watermark for the end of a giant spine.
    if (section->currentPage < section->pageCount - 1 || section->isBuilding()) {
      section->currentPage++;
    } else {
      // We don't want to delete the section mid-render, so grab the semaphore
      {
        RenderLock lock(*this);
        nextPageNumber = 0;
        currentSpineIndex++;
        section.reset();
      }
    }
  } else {
    if (section->currentPage > 0) {
      section->currentPage--;
    } else if (currentSpineIndex > 0) {
      // We don't want to delete the section mid-render, so grab the semaphore
      {
        RenderLock lock(*this);
        nextPageNumber = 0;
        pendingPageJump = std::numeric_limits<uint16_t>::max();
        currentSpineIndex--;
        section.reset();
      }
    }
  }
  lastPageTurnTime = millis();
  requestUpdate();
}

// TODO: Failure handling
void EpubReaderActivity::render(RenderLock&& lock) {
  if (!epub) {
    return;
  }

  // The Quick Menu is an overlay, not a screen: it paints straight onto the page already
  // in the framebuffer and returns, so opening it costs one popup-sized refresh instead of
  // a full page repaint. Closing it calls requestUpdate(), which lands here again with the
  // pop-up inactive and redraws the page normally.
  if (quickMenu.processRender(renderer, mappedInput)) return;

  const auto showPendingSyncSaveError = [this]() {
    if (!pendingSyncSaveError) return;
    pendingSyncSaveError = false;
    GUI.drawPopup(renderer, tr(STR_SAVE_PROGRESS_FAILED));
    scheduleGhostCleanup();
  };

  // A section build failure (e.g. an invalid/corrupt EPUB that fails XML parsing) leaves the
  // "Indexing" popup on screen with no way forward. Surface an explicit error instead of hanging.
  // clearScreen first so the error popup doesn't overlay the stale "Indexing" popup.
  // The diagnostic tail (code + heap at the moment of failure) is appended because these
  // builds fail on-device, where there is usually no serial monitor attached. Code 7 with
  // a low free heap means an allocation failure; code 7 with plenty of heap means the SD
  // write failed. See Section::BuildFailure.
  const auto showBuildError = [this](const Section::BuildFailure code, const uint32_t freeHeap,
                                     const uint32_t maxAlloc) {
    renderer.clearScreen();
    char msg[96];
    if (code == Section::BuildFailure::None) {
      snprintf(msg, sizeof(msg), "%s", tr(STR_INDEX_FAILED));
    } else {
      snprintf(msg, sizeof(msg), "%s (E%d h%uk a%uk)", tr(STR_INDEX_FAILED), static_cast<int>(code), freeHeap / 1024,
               maxAlloc / 1024);
    }
    GUI.drawPopup(renderer, msg);
    scheduleGhostCleanup();
    automaticPageTurnActive = false;
  };

  // edge case handling for sub-zero spine index
  if (currentSpineIndex < 0) {
    currentSpineIndex = 0;
  }
  // based bounds of book, show end of book screen
  if (currentSpineIndex > epub->getSpineItemsCount()) {
    currentSpineIndex = epub->getSpineItemsCount();
  }

  // Show end of book screen
  if (currentSpineIndex == epub->getSpineItemsCount()) {
    // Sole load site: runs on the render task (serialized by RenderLock); the main
    // task only reads the suggestions once the loaded flag is published
    endOfBookOptions.loadOnce(epub->getPath());
    renderer.clearScreen();
    endOfBookOptions.render(renderer, mappedInput);
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    showPendingSyncSaveError();
    return;
  }

  // Publish this book's status bar switch before anything measures or draws the bar.
  // Set here rather than beside each `prefs_ =` assignment so it cannot fall out of
  // step with the prefs the layout below is about to use. Cleared in onExit().
  SETTINGS.setStatusBarOverride(prefs_.statusBarEnabled);

  // Apply screen viewable areas and additional padding
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  computeReaderMargins(orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);

  // Status bar (v2 per-item): reserve top and/or bottom bands. computeReaderMargins
  // already folded the reading margins into orientedMarginTop/Bottom; the band is
  // ADDED on top of that (a real gap between the bar and the text), matching the
  // additive left/right margins. Changing the band shifts the viewport, which
  // re-paginates the section cache like any margin change.
  // A greedy (truncate-off) title can wrap to several lines; reserve the extra band
  // height in whichever edge holds the title so the reading text is pushed clear.
  // Auto page turn shows a one-line countdown in the title slot, so skip the wrap
  // reservation then.
  int sbTitleExtraPx = 0;
  if (!automaticPageTurnActive && SETTINGS.statusBarEnabled() &&
      SETTINGS.sbTitlePos != CrossPointSettings::SB_ANCHOR_OFF && SETTINGS.sbTitleTruncate == 0) {
    std::string sbTitle;
    if (SETTINGS.sbTitleSource == CrossPointSettings::SB_TITLE_CHAPTER) {
      const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
      if (tocIndex != -1) sbTitle = epub->getTocItem(tocIndex).title;
      if (sbTitle.empty()) sbTitle = tr(STR_UNNAMED);
    } else {
      sbTitle = epub->getTitle();
    }
    const int lines = UITheme::getStatusBarV2TitleLines(renderer, sbTitle.c_str());
    sbTitleExtraPx = (lines - 1) * renderer.getLineHeight(UI_10_FONT_ID);
  }
  const bool sbTitleTop = SETTINGS.sbTitlePos >= CrossPointSettings::SB_ANCHOR_TL &&
                          SETTINGS.sbTitlePos <= CrossPointSettings::SB_ANCHOR_TR;
  const int sbTop = UITheme::getInstance().getStatusBarV2TopHeight(true, sbTitleTop ? sbTitleExtraPx : 0);
  const int sbBottom = UITheme::getInstance().getStatusBarV2BottomHeight(true, sbTitleTop ? 0 : sbTitleExtraPx);
  // Auto page turn shows a one-line countdown in the title slot; reserve a top band for it.
  const int autoTurnBand = automaticPageTurnActive ? UITheme::getInstance().getMetrics().statusBarVerticalMargin : 0;
  orientedMarginTop += std::max<int>(sbTop, autoTurnBand);
  orientedMarginBottom += sbBottom;

  const uint16_t viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  const uint16_t viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;
  // Capture for loop()'s lazy partial-extension start (must match this render's layout params).
  buildViewportWidth = viewportWidth;
  buildViewportHeight = viewportHeight;

  const ReaderRenderSpec renderSpec = SETTINGS.readerRenderSpec(viewportWidth, viewportHeight, prefs_);

  if (!section) {
    const auto filepath = epub->getSpineItem(currentSpineIndex).href;
    LOG_DBG("ERS", "Loading file: %s, index: %d", filepath.c_str(), currentSpineIndex);
    section = std::unique_ptr<Section>(new Section(epub, currentSpineIndex, renderer));
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
    // Land this render by content offset when one applies. An explicit bookmark jump
    // (pendingOffsetJump) always wins -- it is a deliberate navigation to a stored content anchor.
    // Otherwise fall back to the settings-change reposition: read after the cache-hit reset above,
    // a spec match means the saved page number still names the same content so there is nothing to
    // reposition, while a page jump or fragment anchor is a deliberate navigation that outranks it.
    const std::optional<uint32_t> offsetJump =
        pendingOffsetJump.has_value() ? pendingOffsetJump
        : (pendingPageJump.has_value() || !pendingAnchor.empty() || currentSpineIndex != cachedSpineIndex)
            ? std::nullopt
            : cachedVisibleTextOffset;
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
      const bool needsFullBuild = pendingPercentJump;
      if (needsFullBuild) {
        GUI.drawPopup(renderer, tr(STR_INDEXING));
        scheduleGhostCleanup();
        // The popup's own refresh is a plain FAST, so force the page that replaces it onto the HALF
        // ghost-cleanup path -- otherwise the "INDEXING" text ghosts under the rendered page.
        // No popup redraws while the framebuffer is lent to the build below;
        // the panel holds the popup displayed above (e-ink is persistent).
        const auto popupFn = [this]() {
          if (renderer.hasFrameBuffer()) GUI.drawPopup(renderer, tr(STR_INDEXING));
          scheduleGhostCleanup();
        };
        // Lend the framebuffer's 48 KB to the blocking full build; restored
        // (white) at scope exit, and the page render below redraws everything.
        GfxRenderer::FrameBufferLoan loan(renderer);
        if (!section->createSectionFile(renderSpec, popupFn)) {
          LOG_ERR("ERS", "Failed to persist page data to SD");
          const auto failure = section->lastFailure();
          const auto failHeap = section->lastFailureFreeHeap();
          const auto failAlloc = section->lastFailureMaxAlloc();
          section.reset();
          loan.end();  // restore before anything draws
          showBuildError(failure, failHeap, failAlloc);
          return;
        }
        loan.end();
      } else {
        // Lay out just enough to show the landing page; loop() builds the rest behind it. Show the
        // indexing popup up front only when the build will actually be slow: a large spine (its
        // whole HTML must be inflated before page 1 can lay out -- the giant single-spine case), or
        // a deep resume/jump that must lay out many pages to reach the landing page. Tiny sections
        // build in a blink and stay popup-free.
        const int target = pendingPageJump.has_value() ? *pendingPageJump : (nextPageNumber < 0 ? 0 : nextPageNumber);
        const bool anchorJump = !pendingAnchor.empty();

        // Landing well inside a partial: the page (or anchor, via the on-disk map) is already
        // servable, so don't restart the extension build now -- it re-lays out the WHOLE chapter
        // from page 0 (minutes of background CPU + SD writes on a giant spine), pure waste when
        // the reader never nears the watermark this session. loop() starts it lazily once the
        // reader is within PARTIAL_REBUILD_START_MARGIN pages of the watermark.
        if (section->isPartial() &&
            (anchorJump ? section->getPageForAnchor(pendingAnchor).has_value()
                        : target + PARTIAL_REBUILD_START_MARGIN < static_cast<int>(section->pageCount))) {
          LOG_DBG("ERS", "Partial covers target %d of %d; deferring extension build", target, section->pageCount);
        } else {
          const size_t spineBytes =
              epub->getCumulativeSpineItemSize(currentSpineIndex) -
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
          } else {
            const bool targetAvailable = target < static_cast<int>(section->pageCount);
            showPopup = !targetAvailable && ((spineBytes > BUILD_POPUP_BYTE_THRESHOLD && willInflate) ||
                                             target > BUILD_POPUP_PAGE_THRESHOLD);
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
          buildPopupPending = !showPopup;
          const unsigned long buildStartMs = millis();
          bool started;
          // Lend the framebuffer's 48 KB across the whole blocking build, not just
          // startBuild's inflation peak. Page layout allocates a per-page text arena,
          // and this loop is the lowest-headroom moment in the system: unlike the
          // background builder in loop() it has no heap gate, so an oversized chapter
          // could fail its arena allocation here and take the whole chapter down with
          // it. The loan is dropped around any popup so drawing still works.
          auto loan = makeUniqueNoThrow<GfxRenderer::FrameBufferLoan>(renderer);
          {
            started = section->startBuild(renderSpec, [this, &loan] {
              loan.reset();
              showBuildPopup();
            });
          }
          if (!started) {
            loan.reset();  // restore before anything draws
            LOG_ERR("ERS", "Failed to start section build");
            const auto failure = section->lastFailure();
            const auto failHeap = section->lastFailureFreeHeap();
            const auto failAlloc = section->lastFailureMaxAlloc();
            section.reset();
            buildPopupPending = false;
            showBuildError(failure, failHeap, failAlloc);
            return;
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
              LOG_ERR("ERS", "Failed during incremental section build");
              const auto failure = section->lastFailure();
              const auto failHeap = section->lastFailureFreeHeap();
              const auto failAlloc = section->lastFailureMaxAlloc();
              section.reset();
              buildPopupPending = false;
              showBuildError(failure, failHeap, failAlloc);
              return;
            }
          }
          loan.reset();  // hand the framebuffer back before the page is drawn
          buildPopupPending = false;
        }
      }
    } else {
      LOG_DBG("ERS", "Cache found, skipping build...");
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
      }
    }
    pendingOffsetJump.reset();  // one-shot explicit jump: consumed on this render

    if (pendingPrefsMigration_ && section->pageCount > 0) {
      // The chapter is now laid out under the book's old settings, so the paragraph the
      // reader is on can be read off it. reloadForReaderPrefsChange() captures that
      // anchor and drops the section; the rebuild below lands on the same paragraph.
      pendingPrefsMigration_ = false;
      ReaderPrefs upgraded = prefs_;
      // A pre-v10 sidecar has no status bar byte, so the struct default (on) stood in
      // while this chapter was laid out. Seed the book from the global setting now, with
      // the position already anchored, because the switch changes the viewport.
      upgraded.statusBarEnabled = SETTINGS.sbEnabled;
      if (std::memcmp(&upgraded, &prefs_, sizeof(ReaderPrefs)) != 0) {
        prefs_ = upgraded;
        writeReaderOverride(prefs_);
        // Already inside render(), which holds the render lock — see dropSectionForRelayout.
        dropSectionForRelayout();
        requestUpdate();
        return;
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

    if (pendingPercentJump && section->pageCount > 0) {
      // Apply the pending percent jump now that we know the new section's page count.
      int newPage = static_cast<int>(pendingSpineProgress * static_cast<float>(section->pageCount));
      if (newPage >= section->pageCount) {
        newPage = section->pageCount - 1;
      }
      section->currentPage = newPage;
      pendingPercentJump = false;
    }

    if (pendingParagraphScan_.has_value() && section->pageCount > 0) {
      // A cross-chapter Go-to-Paragraph landed in this freshly loaded section; scan it
      // for the target local ordinal now that its pages exist.
      section->currentPage = findPageForOrdinal(*section, *pendingParagraphScan_);
      pendingParagraphScan_.reset();
    }
  }

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
      LOG_ERR("ERS", "Failed to start partial extension build");
      const auto failure = section->lastFailure();
      const auto failHeap = section->lastFailureFreeHeap();
      const auto failAlloc = section->lastFailureMaxAlloc();
      section.reset();
      showBuildError(failure, failHeap, failAlloc);
      return;
    }
    // Extend until either the target page exists or the build completes.
    while (!section->isBuildComplete() && section->currentPage >= static_cast<int>(section->pageCount)) {
      if (!section->buildSomeMore(BUILD_PAGES_PER_CHUNK)) {
        LOG_ERR("ERS", "Failed during incremental section build");
        const auto failure = section->lastFailure();
        const auto failHeap = section->lastFailureFreeHeap();
        const auto failAlloc = section->lastFailureMaxAlloc();
        section.reset();
        showBuildError(failure, failHeap, failAlloc);
        return;
      }
    }
  }
  // For an in-progress incremental build, make sure the page we're about to show has been laid out.
  if (section->isBuilding()) {
    while (!section->isBuildComplete() && section->currentPage >= static_cast<int>(section->pageCount)) {
      if (!section->buildSomeMore(BUILD_PAGES_PER_CHUNK)) {
        LOG_ERR("ERS", "Failed during incremental section build");
        const auto failure = section->lastFailure();
        const auto failHeap = section->lastFailureFreeHeap();
        const auto failAlloc = section->lastFailureMaxAlloc();
        section.reset();
        showBuildError(failure, failHeap, failAlloc);
        return;
      }
    }
  }

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
    LOG_DBG("ERS", "No pages to render");
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_CHAPTER), true, EpdFontFamily::REGULAR);
    renderStatusBar();
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    showPendingSyncSaveError();
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    LOG_DBG("ERS", "Page out of bounds: %d (max %d)", section->currentPage, section->pageCount);
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_OUT_OF_BOUNDS), true, EpdFontFamily::REGULAR);
    renderStatusBar();
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    showPendingSyncSaveError();
    return;
  }

  updateBookmarkFlag();

  {
    // Unified page read: the in-progress build's in-RAM table if it has reached the page,
    // otherwise the on-disk file (finalized section, or a partial from a previous session).
    auto p = section->loadPage(section->currentPage);
    if (!p) {
      LOG_ERR("ERS", "Failed to load page from SD - clearing section cache");
      automaticPageTurnActive = false;
      // Retrying rebuilds a transiently corrupt section and usually recovers, but a page that keeps
      // failing would loop forever on a blank screen, so bound the retries before giving up.
      const bool giveUp = ++pageLoadRetryCount > MAX_PAGE_LOAD_RETRIES;
      // Abandon (not suspend) any active build BEFORE clearing: clearCache deletes the files,
      // and the destructor's suspend would otherwise commit tables into a deleted handle.
      section->abandonBuild();
      section->clearCache();
      section.reset();
      if (giveUp) {
        LOG_ERR("ERS", "Page load retry limit reached, aborting");
        pageLoadRetryCount = 0;  // Reset so a later user-initiated navigation can try afresh
        renderer.clearScreen();
        renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::REGULAR);
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

    // Collect footnotes from the loaded page
    currentPageFootnotes = std::move(p->footnotes);

    const auto start = millis();
    renderContents(std::move(p), orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    LOG_DBG("ERS", "Rendered page in %dms", millis() - start);
    lastRenderCompleteMs = millis();
    // A page is only "shown" once it has actually been drawn; the timer for how
    // long it was read starts here, not at the button press.
    if (statsTrackingActive) statsSession.pageShown(millis(), reading_stats::currentLocalDateTime());
  }
  // Only persist when the position actually changed. render() also runs on menu,
  // bookmark and screenshot re-renders, and writeAtomic is several FAT ops for 6 bytes.
  // Every real page turn changes currentPage, so progress durability is unaffected.
  if (currentSpineIndex != lastSavedSpineIndex || section->currentPage != lastSavedPage ||
      section->pageCount != lastSavedPageCount) {
    if (saveProgress(currentSpineIndex, section->currentPage, section->estimatedTotalPages())) {
      lastSavedSpineIndex = currentSpineIndex;
      lastSavedPage = section->currentPage;
      lastSavedPageCount = section->estimatedTotalPages();
    }
  }

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

bool EpubReaderActivity::applyDeferredReposition() {
  if ((!cachedVisibleTextOffset.has_value() && cachedChapterTotalPageCount == 0 &&
       !pendingOrdinalAnchor_.has_value()) ||
      !section || section->isBuilding()) {
    return false;
  }

  // The paragraph anchor outranks both fallbacks: it is the place the reader actually
  // was. The chapter is fully laid out by now, so the paragraph is certainly in it.
  if (pendingOrdinalAnchor_.has_value() && section->pageCount > 0 && currentSpineIndex == cachedSpineIndex) {
    const int ordinalPage = findPageForOrdinal(*section, *pendingOrdinalAnchor_);
    pendingOrdinalAnchor_.reset();
    cachedChapterTotalPageCount = 0;
    cachedVisibleTextOffset.reset();
    if (ordinalPage != section->currentPage) {
      section->currentPage = ordinalPage;
      nextPageNumber = ordinalPage;
      return true;
    }
    return false;
  }

  bool changed = false;
  // Re-derive the page from the saved content offset after a settings reflow.
  // Older 4/6-byte progress files retain the page-fraction fallback.
  if (currentSpineIndex == cachedSpineIndex) {
    int newPage = section->currentPage;
    bool mappedOffset = false;
    if (cachedVisibleTextOffset.has_value()) {
      if (const auto offsetPage = section->getPageForVisibleTextOffset(*cachedVisibleTextOffset)) {
        newPage = *offsetPage;
        mappedOffset = true;
      }
    }
    if (!mappedOffset && cachedChapterTotalPageCount > 0 && section->pageCount != cachedChapterTotalPageCount) {
      const float progress = static_cast<float>(section->currentPage) / static_cast<float>(cachedChapterTotalPageCount);
      newPage = static_cast<int>(progress * static_cast<float>(section->pageCount));
    }
    if (newPage < 0) newPage = 0;
    if (section->pageCount > 0 && newPage >= static_cast<int>(section->pageCount)) {
      newPage = section->pageCount - 1;
    }
    if (newPage != section->currentPage) {
      section->currentPage = newPage;
      changed = true;
    }
  }
  cachedChapterTotalPageCount = 0;  // consumed; don't read cached progress again
  cachedVisibleTextOffset.reset();
  return changed;
}

bool EpubReaderActivity::saveProgress(int spineIndex, int currentPage, int pageCount) {
  std::optional<uint32_t> offset;
  if (section && spineIndex == currentSpineIndex && currentPage >= 0 && currentPage < section->pageCount) {
    // The on-screen page's offset was captured at load; reuse it to avoid a fresh section-file
    // open on every page turn. Any other page (rare) falls back to a direct lookup.
    offset = (currentPage == section->currentPage && currentPageVisibleOffset.has_value())
                 ? currentPageVisibleOffset
                 : section->getVisibleTextOffsetForPage(static_cast<uint16_t>(currentPage));
  }
  return EpubReaderUtils::saveProgress(*epub, spineIndex, currentPage, pageCount, offset);
}

void EpubReaderActivity::rememberCurrentContentOffset() {
  cachedVisibleTextOffset.reset();
  if (section && section->currentPage >= 0 && section->currentPage < section->pageCount) {
    cachedVisibleTextOffset = section->getVisibleTextOffsetForPage(static_cast<uint16_t>(section->currentPage));
  }
}
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
    // Async form: start the waveform and return so the grayscale plane rendering
    // below overlaps the panel's refresh time instead of following it.
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh, overlapRefresh);
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
      auto scratch = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(gwBytes) * STRIP_ROWS);
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
          renderer.beginStripTarget(scratch.get(), y, rows);
          renderer.clearScreen(0x00);
          renderGrayscalePass();
          renderer.endStripTarget();
          renderer.writeGrayscalePlaneStrip(true, scratch.get(), y, rows);
        }
        const auto tGrayLsb = millis();

        // MSB plane.
        renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
        for (int y = 0; y < gh; y += STRIP_ROWS) {
          const int rows = (gh - y < STRIP_ROWS) ? (gh - y) : STRIP_ROWS;
          renderer.beginStripTarget(scratch.get(), y, rows);
          renderer.clearScreen(0x00);
          renderGrayscalePass();
          renderer.endStripTarget();
          renderer.writeGrayscalePlaneStrip(false, scratch.get(), y, rows);
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
    // Fallback path for a controller without strip support. grayscale rendering
    // TODO: Only do this if font supports it
    if (needsAnyGrayscale) {
      // Save the BW frame before the grayscale passes overwrite it, restore
      // after. Only needed when grayscale actually renders.
      if (!renderer.storeBwBuffer()) {
        LOG_ERR("ERS", "Failed to store BW buffer for grayscale render; skipping grayscale this page");
        const auto tEnd = millis();
        LOG_DBG("ERS", "Page render: prewarm=%lums bw_render=%lums display=%lums total=%lums", tPrewarm - t0,
                tBwRender - tPrewarm, tDisplay - tBwRender, tEnd - t0);
        return;
      }
      const auto tBwStore = millis();

      renderer.clearScreen(0x00);
      renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
      renderGrayscalePass();
      renderer.copyGrayscaleLsbBuffers();
      const auto tGrayLsb = millis();

      // Render and copy to MSB buffer
      renderer.clearScreen(0x00);
      renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
      renderGrayscalePass();
      renderer.copyGrayscaleMsbBuffers();
      const auto tGrayMsb = millis();

      // display grayscale part
      renderer.displayGrayBuffer();
      const auto tGrayDisplay = millis();
      renderer.setRenderMode(GfxRenderer::BW);
      renderer.restoreBwBuffer();
      const auto tBwRestore = millis();

      const auto tEnd = millis();
      LOG_DBG("ERS",
              "Page render: prewarm=%lums bw_render=%lums display=%lums bw_store=%lums "
              "gray_lsb=%lums gray_msb=%lums gray_display=%lums bw_restore=%lums total=%lums",
              tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tBwStore - tDisplay, tGrayLsb - tBwStore,
              tGrayMsb - tGrayLsb, tGrayDisplay - tGrayMsb, tBwRestore - tGrayDisplay, tEnd - t0);
    } else {
      // No text AA and no images: BW frame already displayed above, no grayscale
      // to render, so no save/restore.
      const auto tEnd = millis();
      LOG_DBG("ERS", "Page render: prewarm=%lums bw_render=%lums display=%lums total=%lums", tPrewarm - t0,
              tBwRender - tPrewarm, tDisplay - tBwRender, tEnd - t0);
    }
  }
}

void EpubReaderActivity::renderStatusBar() const {
  StatusBarData d;
  d.hasChapters = true;  // EPUB spine sections + TOC always provide chapters
  d.chapterPage = section->currentPage + 1;
  // estimatedTotalPages() keeps "page X / Y" sane while a giant spine is still
  // building (its live pageCount would read off the small build watermark).
  d.chapterPages = static_cast<int>(section->estimatedTotalPages());
  const float chapterProg = (d.chapterPages > 0) ? static_cast<float>(d.chapterPage) / d.chapterPages : 0.0f;
  d.chapterPercent = static_cast<int>(chapterProg * 100 + 0.5f);
  d.bookPercent = static_cast<int>(epub->calculateProgress(currentSpineIndex, chapterProg) * 100 + 0.5f);
  d.bookTitle = epub->getTitle();
  d.chapterTotal = epub->getTocItemsCount();

  const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (tocIndex != -1) {
    d.chapterTitle = epub->getTocItem(tocIndex).title;
    d.chapterNum = tocIndex + 1;
  }
  if (d.chapterTitle.empty()) d.chapterTitle = tr(STR_UNNAMED);
  // Pages turned this sitting. Left at -1 when statistics tracking is off, which
  // hides the item instead of showing a 0 that will never move.
  if (statsTrackingActive) d.sessionPages = static_cast<int>(statsSession.currentSession().pagesTurned);
  d.bookmarked = currentPageBookmarked;

  // Auto page turn: show the countdown in the title slot (wherever the title is
  // anchored). If the title item is off the countdown simply isn't shown.
  if (automaticPageTurnActive && pageTurnDuration > 0) {
    const std::string label = std::string(tr(STR_AUTO_TURN_ENABLED)) + std::to_string(60 * 1000 / pageTurnDuration);
    d.bookTitle = label;
    d.chapterTitle = label;
  }

  // Paperback Look (status bar): thicken only the status-bar glyphs, then reset so
  // nothing drawn afterwards inherits the smear.
  renderer.setPaperbackLook(prefs_.paperbackLookStatus);
  GUI.drawStatusBarV2(renderer, d);
  renderer.setPaperbackLook(false);
}

void EpubReaderActivity::navigateToHref(const std::string& hrefStr, const bool savePosition) {
  if (!epub) return;

  // Push current position onto saved stack
  if (savePosition && section && footnoteDepth < MAX_FOOTNOTE_DEPTH) {
    savedPositions[footnoteDepth] = {currentSpineIndex, section->currentPage};
    footnoteDepth++;
    LOG_DBG("ERS", "Saved position [%d]: spine %d, page %d", footnoteDepth, currentSpineIndex, section->currentPage);
  }

  // Extract fragment anchor (e.g. "#note1" or "chapter2.xhtml#note1")
  std::string anchor;
  const auto hashPos = hrefStr.find('#');
  if (hashPos != std::string::npos && hashPos + 1 < hrefStr.size()) {
    anchor = hrefStr.substr(hashPos + 1);
  }

  // Check for same-file anchor reference (#anchor only)
  bool sameFile = !hrefStr.empty() && hrefStr[0] == '#';

  int targetSpineIndex;
  if (sameFile) {
    targetSpineIndex = currentSpineIndex;
  } else {
    targetSpineIndex = epub->resolveHrefToSpineIndex(hrefStr);
  }

  if (targetSpineIndex < 0) {
    LOG_DBG("ERS", "Could not resolve href: %s", hrefStr.c_str());
    if (savePosition && footnoteDepth > 0) footnoteDepth--;  // undo push
    return;
  }

  {
    RenderLock lock(*this);
    pendingAnchor = std::move(anchor);
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    section.reset();
  }
  requestUpdate();
  LOG_DBG("ERS", "Navigated to spine %d for href: %s", targetSpineIndex, hrefStr.c_str());
}

void EpubReaderActivity::restoreSavedPosition() {
  if (footnoteDepth <= 0) return;
  footnoteDepth--;
  const auto& pos = savedPositions[footnoteDepth];
  LOG_DBG("ERS", "Restoring position [%d]: spine %d, page %d", footnoteDepth, pos.spineIndex, pos.pageNumber);

  {
    RenderLock lock(*this);
    currentSpineIndex = pos.spineIndex;
    nextPageNumber = pos.pageNumber;
    section.reset();
  }
  requestUpdate();
}

void EpubReaderActivity::loadCachedBookmarks() {
  cachedBookmarks.clear();
  if (cachedBookmarks.capacity() < initialBookmarkCacheCapacity) {
    cachedBookmarks.reserve(initialBookmarkCacheCapacity);
  }
  if (!epub) {
    currentPageBookmarked = false;
    return;
  }

  BookmarkFile::load(epub->getPath(), cachedBookmarks);
  updateBookmarkFlag();
}

void EpubReaderActivity::addBookmark() {
  if (!section || !epub) {
    return;
  }
  LOG_DBG("ERS", "Toggle bookmark at spine %d, page %d", currentSpineIndex, section ? section->currentPage : -1);
  int currentPage;
  int pageCount;
  {
    RenderLock lock(*this);
    pageCount = section->estimatedTotalPages();
    currentPage = section->currentPage;
  }

  SavedProgressPosition progress = ProgressMapper::toSavedProgress(epub, getCurrentPosition());
  const ProgressRange pageRange = getPageProgressRange(epub, currentSpineIndex, currentPage, pageCount);

  const size_t bookmarkCountBeforeToggle = cachedBookmarks.size();
  cachedBookmarks.erase(std::remove_if(cachedBookmarks.begin(), cachedBookmarks.end(),
                                       [&](const BookmarkEntry& b) {
                                         return bookmarkMatchesProgress(b, currentSpineIndex, currentPage, pageCount,
                                                                        pageRange);
                                       }),
                        cachedBookmarks.end());
  if (cachedBookmarks.size() != bookmarkCountBeforeToggle) {
    bookmarkRemoved = true;
    currentPageBookmarked = false;
  } else {
    std::string pageText;
    if (currentPage >= 0 && currentPage < pageCount) {
      pageText = section->getTextFromSectionFile();
    }
    BookmarkEntry entry;
    entry.percentage = progress.percentage;
    entry.xpath = progress.xpath;
    entry.summary = BookmarkUtil::sanitizeBookmarkSummary(pageText);
    entry.computedSpineIndex = currentSpineIndex;
    entry.computedChapterPageCount = pageCount;
    entry.computedChapterProgress = currentPage;
    // Record the exact content offset so the bookmark lands correctly after any re-pagination.
    // currentPageVisibleOffset was captured for this very page at its last render.
    const std::optional<uint32_t> offset =
        currentPageVisibleOffset.has_value() ? currentPageVisibleOffset
        : (currentPage >= 0 && currentPage < section->pageCount)
            ? section->getVisibleTextOffsetForPage(static_cast<uint16_t>(currentPage))
            : std::nullopt;
    if (offset.has_value()) {
      entry.visibleTextOffset = *offset;
      entry.hasVisibleTextOffset = true;
    }
    cachedBookmarks.insert(cachedBookmarks.begin(), entry);
    bookmarkRemoved = false;
    currentPageBookmarked = true;
  }

  if (!BookmarkFile::save(epub->getPath(), cachedBookmarks)) {
    LOG_ERR("ERS", "Failed to save bookmarks");
  }
  requestUpdate();
}

void EpubReaderActivity::updateBookmarkFlag() {
  if (!section || !epub || cachedBookmarks.empty()) {
    currentPageBookmarked = false;
    return;
  }
  const int pageCount = section->estimatedTotalPages();
  const ProgressRange pageRange = getPageProgressRange(epub, currentSpineIndex, section->currentPage, pageCount);
  currentPageBookmarked = std::any_of(cachedBookmarks.begin(), cachedBookmarks.end(), [&](const BookmarkEntry& b) {
    return bookmarkMatchesProgress(b, currentSpineIndex, section->currentPage, pageCount, pageRange);
  });
}

ScreenshotInfo EpubReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Epub;
  if (epub) {
    snprintf(info.title, sizeof(info.title), "%s", epub->getTitle().c_str());
    info.spineIndex = currentSpineIndex;
  }
  if (section) {
    info.currentPage = section->currentPage + 1;
    info.totalPages = section->estimatedTotalPages();
    if (epub && epub->getBookSize() > 0 && info.totalPages > 0) {
      const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(info.totalPages);
      int pct = static_cast<int>(epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f + 0.5f);
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      info.progressPercent = pct;
    }
  }
  return info;
}

CrossPointPosition EpubReaderActivity::getCurrentPosition() const {
  const int currentPage = section ? section->currentPage : nextPageNumber;
  const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
  std::optional<uint16_t> paragraphIndex;
  if (section && currentPage >= 0 && currentPage < section->pageCount) {
    const uint16_t paragraphPage =
        currentPage > 0 ? static_cast<uint16_t>(currentPage - 1) : static_cast<uint16_t>(currentPage);
    if (const auto pIdx = section->getParagraphIndexForPage(paragraphPage)) {
      paragraphIndex = *pIdx;
    }
  }

  CrossPointPosition localPos = {currentSpineIndex, currentPage, totalPages};
  if (section && currentPage >= 0 && currentPage < section->pageCount) {
    if (const auto offset = section->getVisibleTextOffsetForPage(static_cast<uint16_t>(currentPage))) {
      localPos.visibleTextOffset = *offset;
      localPos.hasVisibleTextOffset = true;
    }
  }
  if (paragraphIndex.has_value()) {
    localPos.paragraphIndex = *paragraphIndex;
    localPos.hasParagraphIndex = true;
  }
  return localPos;
}
