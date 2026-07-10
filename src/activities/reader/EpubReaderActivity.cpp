#include "EpubReaderActivity.h"

#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <GrowthBounds.h>
#include <HalStorage.h>
#include <I18n.h>
#include <JsonSettingsIO.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_system.h>

#include <algorithm>
#include <functional>
#include <iterator>
#include <limits>

#include "BookInfoActivity.h"
#include "BookmarkEntry.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "EpubReaderBookmarksActivity.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "EpubReaderFootnotesActivity.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "EpubReaderUtils.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncActivity.h"
#include "MappedInputManager.h"
#include "ProgressMapper.h"
#include "QrDisplayActivity.h"
#include "QuoteStorageLimits.h"
#include "QuotesViewerActivity.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "components/StatusBar.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookmarkUtil.h"
#include "util/FavoriteImage.h"
#include "util/ScreenshotUtil.h"

namespace {
// pagesPerRefresh now comes from SETTINGS.getRefreshFrequency()
// pages per minute, first item is 1 to prevent division by zero if accessed
constexpr int PAGE_TURN_RATES[] = {1, 1, 3, 6, 12};
constexpr size_t initialBookmarkCacheCapacity = 16;
constexpr float bookmarkProgressEpsilon = 0.0001f;

int clampPercent(int percent) {
  if (percent < 0) {
    return 0;
  }
  if (percent > 100) {
    return 100;
  }
  return percent;
}

// First-line indent in pixels for the current settings + text-column width.
// Returns -1 in "book" mode so the parser keeps the publisher/CSS indent;
// otherwise maps the indent percentage onto 0..viewportWidth/2 (100% = the
// column's horizontal middle). Fed into the Section cache key so a change
// re-indexes the chapter.
int firstLineIndentPxFor(const int viewportWidth) {
  if (SETTINGS.firstLineIndentMode != CrossPointSettings::FIRST_LINE_INDENT_PERCENT) {
    return -1;
  }
  return viewportWidth * SETTINGS.firstLineIndentPercent / 200;
}

// SD card folder finished books are moved into. Single source of truth for the path.
// constexpr ⇒ lives in flash .rodata, no DRAM cost.
constexpr char READ_FOLDER[] = "/read";

// True if path is inside READ_FOLDER (starts with "<READ_FOLDER>/"). Non-allocating so
// it is cheap to call from loop(), and avoids reintroducing a separate "/Read/" literal.
bool isInReadFolder(const std::string& path) {
  constexpr size_t n = sizeof(READ_FOLDER) - 1;  // length of "/Read" (excludes NUL)
  return path.size() > n && path.compare(0, n, READ_FOLDER) == 0 && path[n] == '/';
}

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

// Pick a non-colliding destination path inside /Read/ for a finished book.
// Mirrors the suffixing scheme used elsewhere: "name.epub" -> "name (2).epub", etc.
std::string buildReadFolderDestination(const std::string& srcPath) {
  const size_t lastSlash = srcPath.rfind('/');
  const std::string filename = (lastSlash != std::string::npos) ? srcPath.substr(lastSlash + 1) : srcPath;

  Storage.mkdir(READ_FOLDER);
  std::string dstPath = std::string(READ_FOLDER) + "/" + filename;
  if (!Storage.exists(dstPath.c_str())) {
    return dstPath;
  }

  const size_t dotPos = filename.rfind('.');
  const std::string base = (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;
  const std::string ext = (dotPos != std::string::npos) ? filename.substr(dotPos) : "";
  int suffix = 2;
  do {
    dstPath = std::string(READ_FOLDER) + "/" + base + " (" + std::to_string(suffix) + ")" + ext;
    suffix++;
  } while (Storage.exists(dstPath.c_str()) && suffix < 100);
  return dstPath;
}

// Relocate a finished book and its cache dir into /read/, keep it in recents by
// repointing its entry to the new path, and repoint the resume pointer too.
// On rename failure: LOG_ERR and leave everything in place (no UI alert subsystem here).
void moveFinishedBookToReadFolder(const std::string& srcPath, const std::string& dstPath,
                                  const std::string& oldCachePath) {
  LOG_INF("ERS", "Moving finished epub: %s -> %s", srcPath.c_str(), dstPath.c_str());
  if (!Storage.rename(srcPath.c_str(), dstPath.c_str())) {
    LOG_ERR("ERS", "Failed to move finished book to '/Read' folder");
    return;
  }

  // Cache dir is keyed by hash of the epub path (see Epub ctor), so it must be re-keyed.
  const std::string newCachePath = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(dstPath));
  if (!oldCachePath.empty() && Storage.exists(oldCachePath.c_str())) {
    if (!Storage.rename(oldCachePath.c_str(), newCachePath.c_str())) {
      LOG_ERR("ERS", "Failed to rename cache dir %s -> %s (non-fatal)", oldCachePath.c_str(), newCachePath.c_str());
    }
  }

  // Keep the book in recents (crossink behavior): repoint the entry to its new
  // location instead of dropping it. updatePath persists on success.
  RECENT_BOOKS.updatePath(srcPath, dstPath, oldCachePath, newCachePath);
  if (APP_STATE.openEpubPath == srcPath) {
    APP_STATE.openEpubPath = dstPath;
    APP_STATE.saveToFile();
  }
}

}  // namespace

void EpubReaderActivity::onEnter() {
  Activity::onEnter();

  if (!epub) {
    return;
  }

  // Configure screen orientation based on settings
  // NOTE: This affects layout math and must be applied before any render calls.
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  epub->setupCacheDir();

  HalFile f;
  if (Storage.openFileForRead("ERS", epub->getCachePath() + "/progress.bin", f)) {
    uint8_t data[6];
    int dataSize = f.read(data, 6);
    if (dataSize == 4 || dataSize == 6) {
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
    }
  }
  // We may want a better condition to detect if we are opening for the first time.
  // This will trigger if the book is re-opened at Chapter 0.
  if (currentSpineIndex == 0) {
    int textSpineIndex = epub->getSpineIndexForTextReference();
    if (textSpineIndex != 0) {
      currentSpineIndex = textSpineIndex;
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

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();

  // Cache last-read progress so the Home recent-books list can show a [NN%]
  // badge without reopening the book. Computed here while epub/section are still
  // live (both are reset below).
  if (epub && epub->getBookSize() > 0 && section && section->pageCount > 0) {
    const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
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
  if (pendingReadFolderMove && epub) {
    const std::string srcPath = epub->getPath();
    const std::string oldCachePath = epub->getCachePath();
    const std::string dstPath = buildReadFolderDestination(srcPath);
    epub.reset();  // release the Epub (and any open handles) before renaming on the SD card
    moveFinishedBookToReadFolder(srcPath, dstPath, oldCachePath);
  } else {
    epub.reset();
  }
}

void EpubReaderActivity::loop() {
  if (!epub) {
    // Should never happen
    finish();
    return;
  }

  // Grab-quote / highlight mode intercepts ALL input while active. Placed before
  // the end-of-book / recents / page-turn logic so selection fully owns the reader.
  if (highlights_.state() != crosspoint::reader::HighlightController::State::NONE) {
    loopHighlightMode();
    return;
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

  // Enter reader menu activity on short-press Confirm. A long-press that fired a bound
  // function (bookmark or KOReader sync) sets ignoreNextConfirmRelease so the release
  // following the hold does not also open the menu.
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ignoreNextConfirmRelease) {
      ignoreNextConfirmRelease = false;
    } else {
      const int currentPage = section ? section->currentPage + 1 : 0;
      const int totalPages = section ? section->pageCount : 0;
      float bookProgress = 0.0f;
      if (epub->getBookSize() > 0 && section && section->pageCount > 0) {
        const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
        bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
      }
      const int bookProgressPercent = clampPercent(static_cast<int>(bookProgress + 0.5f));
      // Resolve the current chapter name for the menu header (mirrors the status
      // bar's CHAPTER_TITLE logic), falling back to "Unnamed" when there is no TOC.
      std::string chapterName = tr(STR_UNNAMED);
      const int menuTocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
      if (menuTocIndex != -1) {
        chapterName = epub->getTocItem(menuTocIndex).title;
      }
      const std::string quotesPath = getQuotesFilePath();
      const bool hasQuotes = !quotesPath.empty() && Storage.exists(quotesPath.c_str());
      // Sleep-wallpaper triage targets the last wallpaper the device actually
      // rendered; only offer it when that file still exists on the card.
      const std::string& lastWallpaper = APP_STATE.lastSleepWallpaperPath;
      const bool hasSleepWallpaper = !lastWallpaper.empty() && Storage.exists(lastWallpaper.c_str());
      const bool wallpaperPaused = APP_STATE.wallpaperRotationPaused;
      const bool wallpaperFavorited = hasSleepWallpaper && FavoriteImage::isFavoritePath(lastWallpaper);
      startActivityForResult(
          makeUniqueNoThrow<EpubReaderMenuActivity>(
              renderer, mappedInput, epub->getTitle(), epub->getAuthor(), chapterName, currentPage, totalPages,
              bookProgressPercent, SETTINGS.orientation, !currentPageFootnotes.empty(), !cachedBookmarks.empty(),
              hasQuotes, hasSleepWallpaper, wallpaperPaused, wallpaperFavorited),
          [this](const ActivityResult& result) {
            // Always apply orientation change even if the menu was cancelled
            const auto& menu = std::get<MenuResult>(result.data);
            applyOrientation(menu.orientation);
            toggleAutoPageTurn(menu.pageTurnOption);
            if (!result.isCancelled) {
              onReaderMenuConfirm(static_cast<EpubReaderMenuActivity::MenuAction>(menu.action));
            }
          });
    }
  }

  // Long-press Confirm runs the user-selected function (SETTINGS.longPressMenuFunction).
  if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
    switch (SETTINGS.longPressMenuFunction) {
      case CrossPointSettings::LP_MENU_BOOKMARK:
        // Hold ~0.4s drops a bookmark at the current page.
        if (mappedInput.getHeldTime() >= ReaderUtils::BOOKMARK_HOLD_MS && !showBookmarkMessage) {
          addBookmark();
          showBookmarkMessage = true;
          ignoreNextConfirmRelease = true;  // Prevent accidental menu open after adding bookmark
          bookmarkMessageTime = millis();
          requestUpdate();
        }
        break;
      case CrossPointSettings::LP_MENU_KOSYNC:
        // Hold ~1s launches KOReader sync. If sync can't run (no credentials stored), fall
        // through so the normal Confirm-release still opens the reader menu.
        if (mappedInput.getHeldTime() >= ReaderUtils::GO_HOME_MS) {
          if (launchKOReaderSync()) {
            ignoreNextConfirmRelease = true;  // sync launched or error shown; suppress menu open
            return;
          }
        }
        break;
      case CrossPointSettings::LP_MENU_DISABLED:
      default:
        break;
    }
  }

  // Long press BACK (1s+) goes to file selection
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= ReaderUtils::GO_HOME_MS) {
    activityManager.goToFileBrowser(epub ? epub->getPath() : "");
    return;
  }

  // Short press BACK goes directly to home (or restores position if viewing footnote)
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_HOME_MS) {
    if (footnoteDepth > 0) {
      restoreSavedPosition();
      return;
    }
    onGoHome();
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
            makeUniqueNoThrow<EpubReaderFootnotesActivity>(renderer, mappedInput, currentPageFootnotes),
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

  const auto [prevTriggered, nextTriggered, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
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

  const bool longPress = !fromTilt && mappedInput.getHeldTime() > ReaderUtils::SKIP_HOLD_MS;

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
      if (currentSpineIndex != sync.spineIndex || (section && section->currentPage != sync.page)) {
        RenderLock lock(*this);
        currentSpineIndex = sync.spineIndex;
        nextPageNumber = sync.page;
        section.reset();
      }
    }
  };

  switch (action) {
    case EpubReaderMenuActivity::MenuAction::HIGHLIGHT_QUOTE: {
      enterHighlightMode();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::VIEW_QUOTES: {
      startActivityForResult(makeUniqueNoThrow<QuotesViewerActivity>(renderer, mappedInput, getQuotesFilePath()),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER: {
      const int spineIdx = currentSpineIndex;
      const std::string path = epub->getPath();
      startActivityForResult(
          makeUniqueNoThrow<EpubReaderChapterSelectionActivity>(renderer, mappedInput, epub, path, spineIdx),
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
    case EpubReaderMenuActivity::MenuAction::BOOK_INFO: {
      if (!epub) {
        LOG_ERR("EPUB", "Cannot show book info without an open EPUB");
        requestUpdate();
        break;
      }
      // Reuse the 1-bit cover thumbnail pipeline (same as the home screen) so the info
      // screen can draw the cover with a single refresh (no grayscale passes needed).
      constexpr int kInfoCoverHeight = 360;
      std::string coverPath;
      if (epub->generateThumbBmp(kInfoCoverHeight)) {
        const std::string path = epub->getThumbBmpPath(kInfoCoverHeight);
        if (Storage.exists(path.c_str())) {
          coverPath = path;
        }
      }
      startActivityForResult(
          makeUniqueNoThrow<BookInfoActivity>(renderer, mappedInput, epub->getTitle(), epub->getAuthor(),
                                             epub->getLanguage(), epub->getDescription(), coverPath),
          [this](const ActivityResult&) { requestUpdate(); });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::FOOTNOTES: {
      startActivityForResult(makeUniqueNoThrow<EpubReaderFootnotesActivity>(renderer, mappedInput, currentPageFootnotes),
                             [this](const ActivityResult& result) {
                               if (!result.isCancelled) {
                                 const auto& footnoteResult = std::get<FootnoteResult>(result.data);
                                 navigateToHref(footnoteResult.href, true);
                               }
                               requestUpdate();
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
          makeUniqueNoThrow<EpubReaderPercentSelectionActivity>(renderer, mappedInput, initialPercent),
          [this](const ActivityResult& result) {
            if (!result.isCancelled) {
              jumpToPercent(std::get<PercentResult>(result.data).percent);
            }
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DISPLAY_QR: {
      if (section && section->currentPage >= 0 && section->currentPage < section->pageCount) {
        std::string fullText = section->getTextFromSectionFile();
        if (!fullText.empty()) {
          startActivityForResult(makeUniqueNoThrow<QrDisplayActivity>(renderer, mappedInput, fullText),
                                 [this](const ActivityResult& result) {});
          break;
        }
      }
      // If no text or page loading failed, just close menu
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SHARE_QR: {
      if (epub) {
        activityManager.goToQRShare(epub->getPath());
      } else {
        requestUpdate();
      }
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
          makeUniqueNoThrow<EpubReaderBookmarksActivity>(renderer, mappedInput, epub, epub->getPath()),
          progressChangeResultHandler);
      break;
    }
    case EpubReaderMenuActivity::MenuAction::TOGGLE_BOOKMARK: {
      addBookmark();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::TOGGLE_PAPERBACK_LOOK:
    case EpubReaderMenuActivity::MenuAction::TOGGLE_PAPERBACK_STATUS:
    case EpubReaderMenuActivity::MenuAction::TOGGLE_RANDOM_ON_BOOT:
      // Handled in-place inside EpubReaderMenuActivity::loop() (flip + persist),
      // so the menu never returns these as a confirmed action. Listed here only
      // to keep the switch exhaustive.
      break;
    case EpubReaderMenuActivity::MenuAction::WALLPAPER_FAVORITE: {
      const std::string lastPath = APP_STATE.lastSleepWallpaperPath;
      if (!lastPath.empty()) {
        const bool makeFavorite = !FavoriteImage::isFavoritePath(lastPath);
        const auto result = FavoriteImage::setFavorite(lastPath, makeFavorite, nullptr);
        if (result == FavoriteImage::SetFavoriteResult::RenameConflict) {
          GUI.drawPopup(renderer, tr(STR_FAVORITE_NAME_EXISTS));
        } else if (result != FavoriteImage::SetFavoriteResult::Success) {
          GUI.drawPopup(renderer, tr(STR_FAVORITE_FAILED));
        } else {
          GUI.drawPopup(renderer, makeFavorite ? tr(STR_FAVORITED) : tr(STR_UNFAVORITED));
        }
      }
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::WALLPAPER_PAUSE_ROTATION: {
      APP_STATE.wallpaperRotationPaused = !APP_STATE.wallpaperRotationPaused;
      APP_STATE.saveToFile();
      GUI.drawPopup(renderer, APP_STATE.wallpaperRotationPaused ? tr(STR_ROTATION_PAUSED) : tr(STR_ROTATION_UNPAUSED));
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::WALLPAPER_MOVE_PAUSE: {
      const std::string lastPath = APP_STATE.lastSleepWallpaperPath;
      if (lastPath.empty()) {
        requestUpdate();
        break;
      }
      if (lastPath.rfind("/sleep pause/", 0) == 0) {
        GUI.drawPopup(renderer, tr(STR_ALREADY_IN_SLEEP_PAUSE));
        requestUpdate();
        break;
      }
      const std::string destDir = "/sleep pause";
      Storage.mkdir(destDir.c_str());
      const auto slashPos = lastPath.find_last_of('/');
      const std::string filename = (slashPos == std::string::npos) ? lastPath : lastPath.substr(slashPos + 1);
      const std::string dstPath = destDir + "/" + filename;
      // Same-volume move: rename is atomic and never loads the file into RAM
      // (the rotation engine trims to /sleep pause the same way).
      const bool moved = Storage.rename(lastPath.c_str(), dstPath.c_str());
      if (moved) {
        FavoriteImage::replacePathReferences(lastPath, dstPath);
        APP_STATE.wallpaperRotationPaused = false;
        APP_STATE.saveToFile();
      }
      GUI.drawPopup(renderer, moved ? tr(STR_MOVED_TO_SLEEP_PAUSE) : tr(STR_MOVE_FAILED));
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::WALLPAPER_DELETE: {
      const std::string lastPath = APP_STATE.lastSleepWallpaperPath;
      if (lastPath.empty()) {
        requestUpdate();
        break;
      }
      const bool removed = Storage.remove(lastPath.c_str());
      if (removed) {
        FavoriteImage::removePathReferences(lastPath);
        APP_STATE.wallpaperRotationPaused = false;
        APP_STATE.saveToFile();
      }
      GUI.drawPopup(renderer, removed ? tr(STR_WALLPAPER_DELETED) : tr(STR_DELETE_FAILED));
      requestUpdate();
      break;
    }
  }
}

bool EpubReaderActivity::launchKOReaderSync() {
  if (!KOREADER_STORE.hasCredentials()) return false;  // no-op: nothing to launch

  const int currentPage = section ? section->currentPage : nextPageNumber;
  const int totalPages = section ? section->pageCount : cachedChapterTotalPageCount;
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
    section.reset();
    epub.reset();
  }
  LOG_DBG("KOSync", "Epub released (heap after: %u)", (unsigned)ESP.getFreeHeap());

  activityManager.replaceActivity(makeUniqueNoThrow<KOReaderSyncActivity>(
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
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
    }

    // Persist the selection so the reader keeps the new orientation on next launch.
    SETTINGS.orientation = orientation;
    SETTINGS.saveToFile();

    // Update renderer orientation to match the new logical coordinate system.
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

    // Reset section to force re-layout in the new orientation.
    section.reset();
  }
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
    if (section) {
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
    }
    section.reset();
  }
}

void EpubReaderActivity::pageTurn(bool isForwardTurn) {
  if (isForwardTurn) {
    APP_STATE.sessionPagesRead++;  // home "pages read" tally; persisted with the rest of the state
    if (section->currentPage < section->pageCount - 1) {
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

  const auto showPendingSyncSaveError = [this]() {
    if (!pendingSyncSaveError) return;
    pendingSyncSaveError = false;
    GUI.drawPopup(renderer, tr(STR_SAVE_PROGRESS_FAILED));
  };

  // A section build failure (e.g. an invalid/corrupt EPUB that fails XML parsing) leaves the
  // "Indexing" popup on screen with no way forward. Surface an explicit error instead of hanging.
  // clearScreen first so the error popup doesn't overlay the stale "Indexing" popup.
  const auto showBuildError = [this]() {
    renderer.clearScreen();
    GUI.drawPopup(renderer, tr(STR_INDEX_FAILED));
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

  // Apply screen viewable areas and additional padding
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  // Uniform margins use screenMargin on every side; otherwise top/bottom are
  // independent while screenMargin stays the horizontal (left/right) margin.
  const uint8_t horizontalMargin = SETTINGS.screenMargin;
  const uint8_t topMargin = SETTINGS.uniformMargins ? SETTINGS.screenMargin : SETTINGS.screenMarginTop;
  const uint8_t bottomMargin = SETTINGS.uniformMargins ? SETTINGS.screenMargin : SETTINGS.screenMarginBottom;
  orientedMarginLeft += horizontalMargin;
  orientedMarginRight += horizontalMargin;

  // v2 status bar reserves a band at the top and/or bottom edge (whichever holds
  // items). The band overlaps the reading margin (max, not sum), matching the old
  // bottom-only behaviour, so changing the bar just re-paginates like a margin
  // change. EPUB always has chapters. When auto page turn is on, the countdown is
  // drawn in the title slot, so reserve at least a top band for it if the title is
  // otherwise hidden there.
  // A greedy (truncate-off) title can wrap to several lines; reserve the extra
  // band height in whichever edge holds the title so the reading text is pushed
  // clear of it. Auto page turn shows a short countdown in the title slot (one
  // line), so skip the wrap reservation then.
  int sbTitleExtraPx = 0;
  if (!automaticPageTurnActive && SETTINGS.sbEnabled && SETTINGS.sbTitlePos != CrossPointSettings::SB_ANCHOR_OFF &&
      SETTINGS.sbTitleTruncate == 0) {
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
  const int autoTurnBand = automaticPageTurnActive ? UITheme::getInstance().getMetrics().statusBarVerticalMargin : 0;
  // The user's top/bottom margin is ADDED on top of the status-bar band (a real
  // gap between the bar and the text), mirroring the direct add for left/right.
  // Previously this was max(margin, band), so any side holding status-bar items
  // swallowed that side's margin until it exceeded the band (~19px+), making
  // top/bottom feel unenforced and asymmetric with the horizontal margins. On an
  // edge with no bar items the band is 0, so the margin applies straight from the
  // screen edge. Margin changes shift the viewport, which re-paginates the cache.
  orientedMarginTop += std::max<int>(sbTop, autoTurnBand) + topMargin;
  orientedMarginBottom += sbBottom + bottomMargin;

  const uint16_t viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  const uint16_t viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;

  if (!section) {
    const auto filepath = epub->getSpineItem(currentSpineIndex).href;
    LOG_DBG("ERS", "Loading file: %s, index: %d", filepath.c_str(), currentSpineIndex);
    section = std::unique_ptr<Section>(new Section(epub, currentSpineIndex, renderer));

    if (!section->loadSectionFile(
            SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(), SETTINGS.extraParagraphSpacing,
            SETTINGS.paragraphAlignment, viewportWidth, viewportHeight, SETTINGS.hyphenationEnabled,
            SETTINGS.embeddedStyle, SETTINGS.imageRendering, SETTINGS.focusReadingEnabled,
            firstLineIndentPxFor(viewportWidth), SETTINGS.wordSpacing, SETTINGS.paragraphSpacing)) {
      LOG_DBG("ERS", "Cache not found, building...");

      GUI.drawPopup(renderer, tr(STR_INDEXING));

      const auto popupFn = [this]() { GUI.drawPopup(renderer, tr(STR_INDEXING)); };

      if (!section->createSectionFile(
              SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(), SETTINGS.extraParagraphSpacing,
              SETTINGS.paragraphAlignment, viewportWidth, viewportHeight, SETTINGS.hyphenationEnabled,
              SETTINGS.embeddedStyle, SETTINGS.imageRendering, SETTINGS.focusReadingEnabled,
              firstLineIndentPxFor(viewportWidth), SETTINGS.wordSpacing, SETTINGS.paragraphSpacing, popupFn)) {
        LOG_ERR("ERS", "Failed to persist page data to SD");
        section.reset();
        showBuildError();
        return;
      }
    } else {
      LOG_DBG("ERS", "Cache found, skipping build...");
    }

    if (pendingPageJump.has_value()) {
      if (*pendingPageJump >= section->pageCount && section->pageCount > 0) {
        section->currentPage = section->pageCount - 1;
      } else {
        section->currentPage = *pendingPageJump;
      }
      pendingPageJump.reset();
    } else {
      section->currentPage = nextPageNumber;
      if (section->currentPage < 0) {
        section->currentPage = 0;
      } else if (section->currentPage >= section->pageCount && section->pageCount > 0) {
        LOG_DBG("ERS", "Clamping cached page %d to %d", section->currentPage, section->pageCount - 1);
        section->currentPage = section->pageCount - 1;
      }
    }

    if (!pendingAnchor.empty()) {
      if (const auto page = section->getPageForAnchor(pendingAnchor)) {
        section->currentPage = *page;
        LOG_DBG("ERS", "Resolved anchor '%s' to page %d", pendingAnchor.c_str(), *page);
      } else {
        LOG_DBG("ERS", "Anchor '%s' not found in section %d", pendingAnchor.c_str(), currentSpineIndex);
      }
      pendingAnchor.clear();
    }

    // handles changes in reader settings and reset to approximate position based on cached progress
    if (cachedChapterTotalPageCount > 0) {
      // only goes to relative position if spine index matches cached value
      if (currentSpineIndex == cachedSpineIndex && section->pageCount != cachedChapterTotalPageCount) {
        float progress = static_cast<float>(section->currentPage) / static_cast<float>(cachedChapterTotalPageCount);
        int newPage = static_cast<int>(progress * section->pageCount);
        section->currentPage = newPage;
      }
      cachedChapterTotalPageCount = 0;  // resets to 0 to prevent reading cached progress again
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
  }

  renderer.clearScreen();

  if (section->pageCount == 0) {
    LOG_DBG("ERS", "No pages to render");
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_CHAPTER), true, EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    showPendingSyncSaveError();
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    LOG_DBG("ERS", "Page out of bounds: %d (max %d)", section->currentPage, section->pageCount);
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_OUT_OF_BOUNDS), true, EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    showPendingSyncSaveError();
    return;
  }

  updateBookmarkFlag();

  {
    auto p = section->loadPageFromSectionFile();
    if (!p) {
      LOG_ERR("ERS", "Failed to load page from SD - clearing section cache");
      section->clearCache();
      section.reset();
      requestUpdate();  // Try again after clearing cache
                        // TODO: prevent infinite loop if the page keeps failing to load for some reason
      automaticPageTurnActive = false;
      showPendingSyncSaveError();
      return;
    }

    // Collect footnotes from the loaded page
    currentPageFootnotes = std::move(p->footnotes);

    const auto start = millis();
    renderContents(std::move(p), orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    LOG_DBG("ERS", "Rendered page in %dms", millis() - start);
  }
  silentIndexNextChapterIfNeeded(viewportWidth, viewportHeight);
  saveProgress(currentSpineIndex, section->currentPage, section->pageCount);

  showPendingSyncSaveError();

  if (pendingScreenshot) {
    pendingScreenshot = false;
    ScreenshotUtil::takeScreenshot(renderer);
  }

  if (showBookmarkMessage) {
    GUI.drawPopup(renderer, bookmarkRemoved ? tr(STR_BOOKMARK_REMOVED) : tr(STR_BOOKMARK_ADDED));
  }
}

void EpubReaderActivity::silentIndexNextChapterIfNeeded(const uint16_t viewportWidth, const uint16_t viewportHeight) {
  if (!epub || !section || section->pageCount < 2) {
    return;
  }

  // Build the next chapter cache while the penultimate page is on screen.
  if (section->currentPage != section->pageCount - 2) {
    return;
  }

  const int nextSpineIndex = currentSpineIndex + 1;
  if (nextSpineIndex < 0 || nextSpineIndex >= epub->getSpineItemsCount()) {
    return;
  }

  Section nextSection(epub, nextSpineIndex, renderer);
  if (nextSection.loadSectionFile(
          SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(), SETTINGS.extraParagraphSpacing,
          SETTINGS.paragraphAlignment, viewportWidth, viewportHeight, SETTINGS.hyphenationEnabled,
          SETTINGS.embeddedStyle, SETTINGS.imageRendering, SETTINGS.focusReadingEnabled,
          firstLineIndentPxFor(viewportWidth), SETTINGS.wordSpacing, SETTINGS.paragraphSpacing)) {
    return;
  }

  LOG_DBG("ERS", "Silently indexing next chapter: %d", nextSpineIndex);
  if (!nextSection.createSectionFile(
          SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(), SETTINGS.extraParagraphSpacing,
          SETTINGS.paragraphAlignment, viewportWidth, viewportHeight, SETTINGS.hyphenationEnabled,
          SETTINGS.embeddedStyle, SETTINGS.imageRendering, SETTINGS.focusReadingEnabled,
          firstLineIndentPxFor(viewportWidth), SETTINGS.wordSpacing, SETTINGS.paragraphSpacing)) {
    LOG_ERR("ERS", "Failed silent indexing for chapter: %d", nextSpineIndex);
  }
}

bool EpubReaderActivity::saveProgress(int spineIndex, int currentPage, int pageCount) {
  return EpubReaderUtils::saveProgress(*epub, spineIndex, currentPage, pageCount);
}

// ── Grab-quote / highlight selection (ported from DX34) ──────────────────────
// State machine + word-position cache live in HighlightController. This activity
// owns rendering (solid frame + inverted word cursor + black quote splash),
// input routing, quote extraction and the atomic SD save.

std::vector<EpubReaderActivity::WordInfo> EpubReaderActivity::buildWordList(const Page& page, const int xOffset,
                                                                            const int yOffset, const int fontId) const {
  std::vector<WordInfo> result;
  for (const auto& el : page.elements) {
    if (el->getTag() != TAG_PageLine) continue;
    const auto& line = static_cast<const PageLine&>(*el);
    const auto& tb = line.getBlock();
    if (!tb) continue;
    for (uint16_t i = 0; i < tb->wordCount(); i++) {
      WordInfo wi;
      wi.x = static_cast<int>(tb->wordXpos(i)) + line.xPos + xOffset;
      wi.y = line.yPos + yOffset;
      wi.style = tb->wordStyle(i);
      wi.width = renderer.getTextWidth(fontId, tb->wordText(i), wi.style);
      wi.text = tb->wordText(i);
      result.push_back(std::move(wi));
    }
  }
  return result;
}

void EpubReaderActivity::rebuildHighlightWordCache(const int xOffset, const int yOffset) {
  std::vector<crosspoint::reader::WordPos> words;
  auto page = section ? section->loadPageFromSectionFile() : nullptr;
  if (page) {
    const int fontId = SETTINGS.getReaderFontId();
    for (const auto& el : page->elements) {
      if (el->getTag() != TAG_PageLine) continue;
      const auto& line = static_cast<const PageLine&>(*el);
      const auto& tb = line.getBlock();
      if (!tb) continue;
      for (uint16_t i = 0; i < tb->wordCount(); i++) {
        crosspoint::reader::WordPos wp;
        wp.x = static_cast<int16_t>(static_cast<int>(tb->wordXpos(i)) + line.xPos + xOffset);
        wp.y = static_cast<int16_t>(line.yPos + yOffset);
        wp.width = static_cast<int16_t>(renderer.getTextWidth(fontId, tb->wordText(i), tb->wordStyle(i)));
        words.push_back(wp);
      }
    }
  }
  highlights_.setWordsForPage(section ? section->currentPage : 0, std::move(words));
}

void EpubReaderActivity::enterHighlightMode() {
  if (!section || section->pageCount == 0) return;
  highlights_.enter();
  requestUpdate();
}

void EpubReaderActivity::exitHighlightMode() {
  highlights_.exit();
  requestUpdate();
}

void EpubReaderActivity::highlightMoveCursor(const int direction) {
  if (!section) return;
  const crosspoint::reader::PageContext ctx{section->currentPage, section->pageCount, highlights_.wordCount()};
  const auto r = highlights_.moveCursor(direction, ctx);
  if (r.pageDelta != 0) section->currentPage += r.pageDelta;
  if (r.stateChanged) requestUpdate();
}

void EpubReaderActivity::highlightMoveCursorLine(const int direction) {
  if (!section) return;
  const crosspoint::reader::PageContext ctx{section->currentPage, section->pageCount, highlights_.wordCount()};
  const auto r = highlights_.moveCursorLine(direction, ctx);
  if (r.pageDelta != 0) section->currentPage += r.pageDelta;
  if (r.stateChanged) requestUpdate();
}

void EpubReaderActivity::highlightConfirmSelection() {
  if (!section) return;
  const auto r = highlights_.confirm(currentSpineIndex, section->currentPage, millis());
  if (r.pageDelta != 0) section->currentPage += r.pageDelta;
  // Quote text is NOT extracted here — it is deferred to the save (after the 3s
  // hold) so the inverted highlight appears instantly on Confirm with no
  // SD-page-load stall. The overlay is drawn from cached word geometry.
  if (r.stateChanged) requestUpdate();
}

void EpubReaderActivity::handleHighlightInput() {
  using Button = MappedInputManager::Button;
  if (!section) {
    exitHighlightMode();
    return;
  }
  // Back cancels selection.
  if (mappedInput.wasReleased(Button::Back)) {
    exitHighlightMode();
    return;
  }
  // Confirm (release) advances the selection state machine.
  if (mappedInput.wasReleased(Button::Confirm)) {
    highlightConfirmSelection();
    return;
  }
  // Up/Down = move cursor by line.
  if (mappedInput.wasPressed(Button::Up)) {
    highlightMoveCursorLine(-1);
    return;
  }
  if (mappedInput.wasPressed(Button::Down)) {
    highlightMoveCursorLine(+1);
    return;
  }
  // Left / page-back = previous word.
  if (mappedInput.wasPressed(Button::PageBack) || mappedInput.wasPressed(Button::Left)) {
    highlightMoveCursor(-1);
    return;
  }
  // Right / page-forward = next word.
  if (mappedInput.wasPressed(Button::PageForward) || mappedInput.wasPressed(Button::Right)) {
    highlightMoveCursor(+1);
    return;
  }
}

void EpubReaderActivity::loopHighlightMode() {
  using State = crosspoint::reader::HighlightController::State;
  // SHOW_UNDERLINE: hold the inverted selection for kUnderlineTimeoutMs, then
  // extract + save the quote and exit (extraction deferred to here so Confirm
  // shows the highlight instantly).
  if (highlights_.state() == State::SHOW_UNDERLINE) {
    if (highlights_.underlineTimedOut(millis())) {
      const std::string quote = extractQuoteText();
      if (!quote.empty()) saveQuoteToFile(quote);
      exitHighlightMode();
    }
    return;
  }
  handleHighlightInput();
}

void EpubReaderActivity::renderHighlights(const Page& page, const int fontId, const int xOffset, const int yOffset) {
  using State = crosspoint::reader::HighlightController::State;
  if (!section) return;
  // Rebuild the word cache if the page changed since the last selection render.
  if (!highlights_.wordCacheValidFor(section->currentPage)) {
    rebuildHighlightWordCache(xOffset, yOffset);
  }
  const auto& wordList = highlights_.words();
  if (wordList.empty()) return;

  const int wordCount = static_cast<int>(wordList.size());
  const int textHeight = renderer.getTextHeight(fontId);

  // Inverted cursor: solid black box behind the word, white glyph on top.
  const auto drawCursor = [&](const crosspoint::reader::WordPos& cw, const WordInfo* wi) {
    constexpr int pad = 2;
    const int bx = (cw.x > pad) ? cw.x - pad : 0;
    const int by = (cw.y > pad) ? cw.y - pad : 0;
    const int bw = cw.width + (cw.x - bx) + pad;
    const int bh = textHeight + (cw.y - by) + pad;
    renderer.fillRect(bx, by, bw, bh, true);
    if (wi != nullptr && !wi->text.empty()) {
      renderer.drawText(fontId, wi->x, wi->y, wi->text.c_str(), false, wi->style);
    }
  };

  const auto state = highlights_.state();
  std::vector<WordInfo> infoList = buildWordList(page, xOffset, yOffset, fontId);
  const auto wordInfoAt = [&](int idx) -> const WordInfo* {
    if (idx < 0 || idx >= static_cast<int>(infoList.size())) return nullptr;
    return &infoList[idx];
  };

  if (state == State::SELECT_START) {
    const int cursorIdx = highlights_.cursorIndex() >= wordCount ? wordCount - 1 : highlights_.cursorIndex();
    if (cursorIdx >= 0 && cursorIdx < wordCount) {
      drawCursor(wordList[cursorIdx], wordInfoAt(cursorIdx));
    }
  } else if (state == State::SELECT_END) {
    const int endIdx = highlights_.endWordIndex();
    if (section->currentPage == highlights_.endPage() && endIdx >= 0 && endIdx < wordCount) {
      drawCursor(wordList[endIdx], wordInfoAt(endIdx));
    }
  } else if (state == State::SHOW_UNDERLINE) {
    // Final confirm hold: invert the whole selected run on this page (black box,
    // white glyph) for kUnderlineTimeoutMs, then loopHighlightMode() saves.
    const int startPage = highlights_.startPage();
    const int endPage = highlights_.endPage();
    const int startWord = highlights_.startWordIndex();
    const int endWord = highlights_.endWordIndex();
    int selStart = -1;
    int selEnd = -1;
    if (section->currentPage == startPage && section->currentPage == endPage) {
      selStart = startWord;
      selEnd = endWord;
    } else if (section->currentPage == startPage) {
      selStart = startWord;
      selEnd = wordCount - 1;
    } else if (section->currentPage == endPage) {
      selStart = 0;
      selEnd = endWord;
    } else if (section->currentPage > startPage && section->currentPage < endPage) {
      selStart = 0;
      selEnd = wordCount - 1;
    }
    if (selStart >= 0 && selEnd >= 0) {
      if (selStart >= wordCount) selStart = wordCount - 1;
      if (selEnd >= wordCount) selEnd = wordCount - 1;
      // Fill a CONTINUOUS black bar per line (first word's left to last word's
      // right, inter-word spaces included), then redraw the words in white on
      // top. Words on a line share y and are ordered left-to-right in wordList.
      constexpr int pad = 2;
      int i = selStart;
      while (i <= selEnd) {
        const int lineY = wordList[i].y;
        int j = i;
        int minX = wordList[i].x;
        int maxX = wordList[i].x + wordList[i].width;
        while (j <= selEnd && wordList[j].y == lineY) {
          if (wordList[j].x < minX) minX = wordList[j].x;
          if (wordList[j].x + wordList[j].width > maxX) maxX = wordList[j].x + wordList[j].width;
          j++;
        }
        const int bx = (minX > pad) ? minX - pad : 0;
        const int by = (lineY > pad) ? lineY - pad : 0;
        const int bw = (maxX - bx) + pad;
        const int bh = textHeight + (lineY - by) + pad;
        renderer.fillRect(bx, by, bw, bh, true);
        for (int k = i; k < j; k++) {
          const WordInfo* wi = wordInfoAt(k);
          if (wi != nullptr && !wi->text.empty()) {
            renderer.drawText(fontId, wi->x, wi->y, wi->text.c_str(), false, wi->style);
          }
        }
        i = j;
      }
    }
  }
}

std::string EpubReaderActivity::extractQuoteText() {
  const int startPage = highlights_.startPage();
  const int endPage = highlights_.endPage();
  const int startWord = highlights_.startWordIndex();
  const int endWord = highlights_.endWordIndex();
  if (startPage < 0 || endPage < 0 || !section) return "";
  if (startWord < 0 || endWord < 0) return "";

  constexpr size_t kMaxQuoteLength = 8192;
  std::string result;
  const int fontId = SETTINGS.getReaderFontId();

  // loadPageFromSectionFile() keys off section->currentPage; drive it across the
  // selected range and restore afterwards. Offsets don't affect the extracted
  // string (geometry unused here), so pass 0/0.
  const int savedPage = section->currentPage;
  for (int pg = startPage; pg <= endPage; pg++) {
    section->currentPage = pg;
    auto page = section->loadPageFromSectionFile();
    if (!page) continue;
    auto wordList = buildWordList(*page, 0, 0, fontId);
    if (wordList.empty()) continue;

    int startIdx = (pg == startPage) ? startWord : 0;
    int endIdx = (pg == endPage) ? endWord : static_cast<int>(wordList.size()) - 1;
    if (startIdx < 0) startIdx = 0;
    if (endIdx >= static_cast<int>(wordList.size())) endIdx = static_cast<int>(wordList.size()) - 1;

    for (int i = startIdx; i <= endIdx; i++) {
      if (!result.empty()) {
        const char first = wordList[i].text.empty() ? '\0' : wordList[i].text[0];
        if (first != ',' && first != '.' && first != ';' && first != ':' && first != '!' && first != '?' &&
            first != ')' && first != '"') {
          result += ' ';
        }
      }
      result += wordList[i].text;
      if (result.size() >= kMaxQuoteLength) break;
    }
    if (result.size() >= kMaxQuoteLength) break;
  }
  section->currentPage = savedPage;
  return result;
}

std::string EpubReaderActivity::getChapterTitle() const {
  if (!epub) return "";
  const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (tocIndex != -1) {
    return epub->getTocItem(tocIndex).title;
  }
  return "Chapter " + std::to_string(currentSpineIndex + 1);
}

std::string EpubReaderActivity::getQuotesFilePath() const {
  if (!epub) return "";
  const std::string bookPath = epub->getPath();
  const auto dotPos = bookPath.rfind('.');
  const std::string basePath = (dotPos != std::string::npos) ? bookPath.substr(0, dotPos) : bookPath;
  return basePath + "_QUOTES.txt";
}

void EpubReaderActivity::saveQuoteToFile(const std::string& quote) {
  if (!epub || quote.empty()) return;

  const std::string quotesPath = getQuotesFilePath();
  const std::string tmpPath = quotesPath + ".tmp";
  const std::string bakPath = quotesPath + ".bak";
  const std::string chapterTitle = getChapterTitle();
  const std::string entry = "[" + chapterTitle + "]\n" + quote + "\n---\n\n";

  size_t existingSize = 0;
  if (Storage.exists(quotesPath.c_str())) {
    HalFile existing;
    if (!Storage.openFileForRead("HLT", quotesPath, existing)) {
      LOG_ERR("HLT", "Failed to inspect existing quote file");
      return;
    }
    existingSize = existing.size();
  }
  if (!memory::canGrowWithinLimit(existingSize, entry.size(), quote_storage::MAX_FILE_BYTES)) {
    LOG_ERR("HLT", "Quote file reached %u byte limit", static_cast<unsigned>(quote_storage::MAX_FILE_BYTES));
    return;
  }

  // Atomic read-modify-write: copy existing primary into .tmp, append the new
  // entry, then rotate primary -> .bak and .tmp -> primary. A torn write leaves
  // .bak as the prior good state.
  if (Storage.exists(tmpPath.c_str())) {
    Storage.remove(tmpPath.c_str());
  }

  HalFile dst;
  if (!Storage.openFileForWrite("HLT", tmpPath, dst)) {
    LOG_ERR("HLT", "Failed to open quotes tmp for writing: %s", tmpPath.c_str());
    return;
  }

  if (Storage.exists(quotesPath.c_str())) {
    HalFile src;
    if (Storage.openFileForRead("HLT", quotesPath, src)) {
      uint8_t buffer[512];
      while (src.available()) {
        const int rd = src.read(buffer, sizeof(buffer));
        if (rd <= 0) break;
        if (dst.write(buffer, rd) != static_cast<size_t>(rd)) {
          LOG_ERR("HLT", "Failed to copy existing quotes into tmp");
          src.close();
          dst.close();
          Storage.remove(tmpPath.c_str());
          return;
        }
      }
      src.close();
    }
  }

  if (dst.write(entry.c_str(), entry.size()) != entry.size()) {
    LOG_ERR("HLT", "Failed to append new quote to tmp");
    dst.close();
    Storage.remove(tmpPath.c_str());
    return;
  }
  dst.flush();
  dst.close();

  if (Storage.exists(bakPath.c_str())) {
    Storage.remove(bakPath.c_str());
  }
  if (Storage.exists(quotesPath.c_str())) {
    if (!Storage.rename(quotesPath.c_str(), bakPath.c_str())) {
      LOG_ERR("HLT", "Failed to rotate %s -> %s", quotesPath.c_str(), bakPath.c_str());
      Storage.remove(tmpPath.c_str());
      return;
    }
  }
  if (!Storage.rename(tmpPath.c_str(), quotesPath.c_str())) {
    LOG_ERR("HLT", "Failed to promote quotes tmp to %s", quotesPath.c_str());
    if (Storage.exists(bakPath.c_str())) {
      if (Storage.rename(bakPath.c_str(), quotesPath.c_str())) {
        LOG_INF("HLT", "Restored quotes from .bak after promote failure");
      }
    }
    return;
  }
  LOG_DBG("HLT", "Quote saved to %s", quotesPath.c_str());
}
// ── End grab-quote / highlight selection ─────────────────────────────────────
void EpubReaderActivity::renderContents(std::unique_ptr<Page> page, const int orientedMarginTop,
                                        const int orientedMarginRight, const int orientedMarginBottom,
                                        const int orientedMarginLeft) {
  const auto t0 = millis();
  const int fontId = SETTINGS.getReaderFontId();

  // Vertically distribute the page's leftover space so a full page of text isn't
  // pinned against the top status bar with all the slack pooling at the bottom
  // (uniform top/bottom margins otherwise look asymmetric because pagination
  // leaves the sub-line remainder at the bottom). Shift the whole page down by
  // half the leftover, capped at half a line so a short chapter-end page is only
  // nudged, never floated to the middle. Only the text/image content moves; the
  // reserved margins, status bars and grab-quote frame stay put.
  const float lineCompression = SETTINGS.getReaderLineCompression();
  const int lineHeightPx = static_cast<int>(renderer.getLineHeight(fontId) * lineCompression);
  const int viewportHeightPx = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;
  const int contentHeightPx = page->usedHeightPx(renderer, fontId, lineCompression);
  const int verticalSlack = std::max(0, viewportHeightPx - contentHeightPx);
  const int contentTop = orientedMarginTop + std::min(verticalSlack, lineHeightPx) / 2;

  // Grab-quote / highlight mode renders on an isolated BW path so the normal
  // image/grayscale/refresh machinery below is untouched (snappy + ghosting laws).
  // All states (cursor pick + final 3s confirm) render the page + solid frame +
  // an inverted overlay (black box, white glyph) on the relevant words — no
  // separate full-screen splash (that stalled on SD page-loads + full black refresh).
  if (highlights_.state() != crosspoint::reader::HighlightController::State::NONE) {
    renderer.setPaperbackLook(false);
    renderer.setRenderMode(GfxRenderer::BW);
    auto* hlFcm = renderer.getFontCacheManager();
    auto hlScope = hlFcm->createPrewarmScope();
    page->render(renderer, fontId, orientedMarginLeft, contentTop);  // scan pass
    hlScope.endScanAndPrewarm();
    page->render(renderer, fontId, orientedMarginLeft, contentTop);  // real render
    // Solid frame marking selection mode.
    constexpr int frameOffset = 6;
    constexpr int frameThickness = 5;
    const int vpH = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;
    const int bx = orientedMarginLeft - frameOffset;
    const int by = orientedMarginTop - frameOffset;
    const int bw = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight + 2 * frameOffset;
    const int bh = vpH + 2 * frameOffset;
    for (int t = 0; t < frameThickness; ++t) {
      renderer.drawRect(bx + t, by + t, bw - 2 * t, bh - 2 * t, true);
    }
    renderHighlights(*page, fontId, orientedMarginLeft, contentTop);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);  // snappy cursor moves + instant confirm
    renderer.setPaperbackLook(false);
    return;
  }

  // Paperback Look (body): thicken the reader page glyphs while this frame's
  // body text is drawn. Reset to false at the end so the status bar (own flag)
  // and any following menu/overlay render thin. The scan pass draws nothing.
  renderer.setPaperbackLook(SETTINGS.paperbackLookBody);

  // Font prewarm: scan pass accumulates text, then prewarm, then real render
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  page->render(renderer, fontId, orientedMarginLeft, contentTop);  // scan pass
  scope.endScanAndPrewarm();
  const auto tPrewarm = millis();

  const bool pageHasImages = page->hasImages();
  // Text Anti-Aliasing is permanently OFF. On this e-ink panel the greyscale
  // glyph-edge pass is imperceptible, yet it forces a fading grey refresh after
  // every page (very visible on the X3: crisp black text lightens ~0.5s later).
  // Images still use greyscale via pageHasImages below. The setting is removed
  // from the UI and this is hardcoded so it can never be re-enabled.
  const bool needsTextGrayscale = false;
  const bool needsAnyGrayscale = needsTextGrayscale || pageHasImages;
  auto renderGrayscalePass = [&]() {
    if (needsTextGrayscale) {
      page->render(renderer, fontId, orientedMarginLeft, contentTop);
    } else {
      page->renderImages(renderer, fontId, orientedMarginLeft, contentTop);
    }
  };

  page->render(renderer, fontId, orientedMarginLeft, contentTop);
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
      renderer.fillRect(imgX + orientedMarginLeft, imgY + contentTop, imgW, imgH, false);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);

      // Re-render page content to restore images into the blanked area
      // Status bar is not re-rendered here to avoid reading stale dynamic values (e.g. battery %)
      page->render(renderer, fontId, orientedMarginLeft, contentTop);
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
    pagesUntilFullRefresh = 1;
  } else {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  }
  const auto tDisplay = millis();

  // Tiled grayscale: render each plane band-by-band into a small scratch and
  // stream straight to the controller, leaving the BW framebuffer intact so no
  // full-frame storeBwBuffer is needed; controller RAM is re-synced from the
  // live framebuffer afterward. The page is re-rendered ceil(H/STRIP_ROWS) times
  // per plane, but renderCharImpl culls out-of-band glyphs before decode so the
  // cost stays close to one render. Both text (drawPixel) and images
  // (DirectPixelWriter) honor the active strip target.
  if (needsAnyGrayscale && renderer.supportsStripGrayscale()) {
    constexpr int STRIP_ROWS = 80;
    const int gh = renderer.getDisplayHeight();
    const int gwBytes = renderer.getDisplayWidthBytes();

    auto scratch = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(gwBytes) * STRIP_ROWS);
    if (!scratch) {
      LOG_ERR("ERS", "OOM: grayscale strip scratch (%d bytes); skipping AA this page", gwBytes * STRIP_ROWS);
    } else {
      // Bands may be streamed in any order: X4 windows each via setRamArea, X3
      // via PTL.
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
        renderer.setPaperbackLook(false);
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

  // Clear the body flag so any following menu/overlay renders thin. (The status
  // bar draw above already left it false via renderStatusBar; this covers all
  // exit paths.)
  renderer.setPaperbackLook(false);
}

void EpubReaderActivity::renderStatusBar() const {
  StatusBarData d;
  d.hasChapters = true;  // EPUB spine sections + TOC always provide chapters
  d.chapterPage = section->currentPage + 1;
  d.chapterPages = section->pageCount;
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
  d.bookmarked = currentPageBookmarked;

  // Auto page turn: show the countdown in the title slot (wherever the title is
  // anchored). If the title item is off the countdown simply isn't shown.
  if (automaticPageTurnActive && pageTurnDuration > 0) {
    const std::string label = std::string(tr(STR_AUTO_TURN_ENABLED)) + std::to_string(60 * 1000 / pageTurnDuration);
    d.bookTitle = label;
    d.chapterTitle = label;
  }

  // Paperback Look (status bar): thicken only the status-bar glyphs, then reset
  // so nothing drawn afterwards inherits the smear.
  renderer.setPaperbackLook(SETTINGS.paperbackLookStatus);
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

  const std::string bmPath = BookmarkUtil::getBookmarkPath(epub->getPath());
  if (Storage.exists(bmPath.c_str())) {
    String json = Storage.readFile(bmPath.c_str());
    if (!json.isEmpty()) {
      JsonSettingsIO::loadBookmarks(cachedBookmarks, json.c_str());
    }
  }
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
    pageCount = section->pageCount;
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
    cachedBookmarks.insert(cachedBookmarks.begin(), entry);
    bookmarkRemoved = false;
    currentPageBookmarked = true;
  }

  const std::string path = BookmarkUtil::getBookmarkPath(epub->getPath());
  const std::string bookmarksDir = BookmarkUtil::getBookmarksDir();
  Storage.mkdir(bookmarksDir.c_str());
  const bool ok = JsonSettingsIO::saveBookmarks(cachedBookmarks, path.c_str());
  if (!ok) {
    LOG_ERR("ERS", "Failed to save bookmarks to: %s", path.c_str());
  }
  requestUpdate();
}

void EpubReaderActivity::updateBookmarkFlag() {
  if (!section || !epub || cachedBookmarks.empty()) {
    currentPageBookmarked = false;
    return;
  }
  const ProgressRange pageRange =
      getPageProgressRange(epub, currentSpineIndex, section->currentPage, section->pageCount);
  currentPageBookmarked = std::any_of(cachedBookmarks.begin(), cachedBookmarks.end(), [&](const BookmarkEntry& b) {
    return bookmarkMatchesProgress(b, currentSpineIndex, section->currentPage, section->pageCount, pageRange);
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
    info.totalPages = section->pageCount;
    if (epub && epub->getBookSize() > 0 && section->pageCount > 0) {
      const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
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
  const int totalPages = section ? section->pageCount : cachedChapterTotalPageCount;
  std::optional<uint16_t> paragraphIndex;
  if (section && currentPage >= 0 && currentPage < section->pageCount) {
    const uint16_t paragraphPage =
        currentPage > 0 ? static_cast<uint16_t>(currentPage - 1) : static_cast<uint16_t>(currentPage);
    if (const auto pIdx = section->getParagraphIndexForPage(paragraphPage)) {
      paragraphIndex = *pIdx;
    }
  }

  CrossPointPosition localPos = {currentSpineIndex, currentPage, totalPages};
  if (paragraphIndex.has_value()) {
    localPos.paragraphIndex = *paragraphIndex;
    localPos.hasParagraphIndex = true;
  }
  return localPos;
}
