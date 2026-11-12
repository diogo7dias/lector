#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <NearbyPositionReceive.h>

#include <algorithm>
#include <limits>

#include "../../util/BookmarkFile.h"
#include "BookmarkEntry.h"
#include "DictionaryWordSelectActivity.h"
#include "EpubReaderActivity.h"
#include "EpubReaderSearchActivity.h"
#include "EpubReaderUtils.h"
#include "ProgressMapper.h"
#include "QuoteSelectActivity.h"
#include "ReaderUtils.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "util/BookProgressFile.h"
#include "util/BookmarkUtil.h"
#include "util/DeferredFavorite.h"

namespace {
// Spine index and page packed into one key so the debouncer can compare positions without
// knowing the reader's layout. Both fit: a spine has fewer than 65536 items and a chapter
// fewer than 65536 pages.
uint32_t positionKeyFor(const int spineIndex, const int currentPage) {
  return (static_cast<uint32_t>(spineIndex) << 16) | static_cast<uint16_t>(currentPage);
}

constexpr float bookmarkProgressEpsilon = 0.0001f;

struct ProgressRange {
  float start;
  float end;
};

ProgressRange getPageProgressRange(Epub& epub, const int spineIndex, const int page, const int pageCount) {
  if (pageCount <= 1) {
    return {epub.calculateProgress(spineIndex, 0.0f), epub.calculateProgress(spineIndex, 1.0f)};
  }

  const float step = 1.0f / static_cast<float>(pageCount - 1);
  const float anchor = std::clamp(static_cast<float>(page) * step, 0.0f, 1.0f);
  const float start = std::max(0.0f, anchor - (step * 0.5f));
  const float end = std::min(1.0f, anchor + (step * 0.5f));
  return {epub.calculateProgress(spineIndex, start), epub.calculateProgress(spineIndex, end)};
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

void EpubReaderActivity::onBookmarkJumpResult(const ActivityResult& result) {
  loadCachedBookmarks();
  if (!result.isCancelled) {
    const auto& sync = std::get<ProgressChangeResult>(result.data);

    // Preferred path: a bookmark carrying an exact content offset. It is immune to
    // re-pagination, so resolve by content instead of trusting a page number saved under
    // possibly-different settings.
    if (sync.hasVisibleTextOffset && sync.spineIndex >= 0 && sync.spineIndex < epub->getSpineItemsCount()) {
      RenderLock lock(*this);
      recordJumpOrigin();
      jumpToContentOffset(sync.spineIndex, sync.visibleTextOffset, sync.page);
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
          ProgressMapper::toCrossPoint(*epub, {sync.xpath, sync.percentage}, renderer, currentSpineIndex, totalPages);
      targetSpineIndex = fallback.spineIndex;
      targetPage = fallback.pageNumber;
    }

    if (targetSpineIndex < 0 || targetSpineIndex >= epub->getSpineItemsCount()) return;

    // Any explicit selection supersedes the session-start resume/reflow anchor,
    // including a selection that is already active.
    RenderLock lock(*this);
    recordJumpOrigin();
    clearDeferredReposition();

    if (currentSpineIndex != targetSpineIndex) {
      currentSpineIndex = targetSpineIndex;
      nextPageNumber = targetPage;
      section.reset();
    } else if (section && section->currentPage != targetPage) {
      const int clampedTargetPage = std::max(0, targetPage);
      section->currentPage = clampedTargetPage;
    } else if (!section) {
      nextPageNumber = targetPage;
    }
  }
}

void EpubReaderActivity::returnToLastPage() {
  currentSpineIndex = std::max(epub->getSpineItemsCount() - 1, 0);
  nextPageNumber = 0;
  // The "last page" sentinel: render() clamps it to the chapter's real last page.
  pendingPageJump = std::numeric_limits<uint16_t>::max();
  scheduleGhostCleanup();
  requestUpdate();
}

bool EpubReaderActivity::saveProgress(int spineIndex, int currentPage, int pageCount) {
  std::optional<uint32_t> offset;
  if (!sortesMode && section && spineIndex == currentSpineIndex && currentPage >= 0 &&
      currentPage < section->pageCount) {
    // The on-screen page's offset was captured at load; reuse it to avoid a fresh section-file
    // open on every page turn. Any other page (rare) falls back to a direct lookup.
    offset = (currentPage == section->currentPage && currentPageVisibleOffset.has_value())
                 ? currentPageVisibleOffset
                 : section->getVisibleTextOffsetForPage(static_cast<uint16_t>(currentPage));
  }
  if (!EpubReaderUtils::saveProgress(*epub, spineIndex, currentPage, pageCount, offset, sortesMode)) {
    return false;
  }
  // Every write clears the batch, whoever asked for it: an explicit save (sync, nearby
  // send, chapter jump) leaves nothing behind for the flush on exit to repeat.
  progressSaveDebouncer.markPersisted(positionKeyFor(spineIndex, currentPage), static_cast<uint32_t>(pageCount));
  return true;
}

void EpubReaderActivity::queueProgressSave(const int spineIndex, const int currentPage, const int pageCount) {
  // Only marks the save due; loop() writes it once the reader pauses, so the SD write
  // never sits inside the page turn that made it due.
  if (progressSaveDebouncer.observe(positionKeyFor(spineIndex, currentPage), static_cast<uint32_t>(pageCount))) {
    progressSaveDue = true;
  }
}

bool EpubReaderActivity::flushQueuedProgress() {
  if (!progressSaveDebouncer.hasPending() || !epub) {
    return true;
  }
  const uint32_t positionKey = progressSaveDebouncer.lastObservedPosition();
  return saveProgress(static_cast<int>(positionKey >> 16), static_cast<int>(positionKey & 0xFFFFU),
                      static_cast<int>(progressSaveDebouncer.lastObservedMetadata()));
}

void EpubReaderActivity::cacheSectionPosition() {
  if (!section) return;
  rememberCurrentContentOffset();
  cachedSpineIndex = currentSpineIndex;
  cachedChapterTotalPageCount = section->pageCount;
  nextPageNumber = section->currentPage;
}

void EpubReaderActivity::rememberCurrentContentOffset() {
  cachedVisibleTextOffset.reset();
  if (section && section->currentPage >= 0 && section->currentPage < section->pageCount) {
    cachedVisibleTextOffset = section->getVisibleTextOffsetForPage(static_cast<uint16_t>(section->currentPage));
  }
}

void EpubReaderActivity::loadCachedBookmarks() {
  cachedBookmarks.clear();
  if (!epub) {
    return;
  }

  BookmarkFile::load(epub->getPath(), cachedBookmarks);
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

  SavedProgressPosition progress = ProgressMapper::toSavedProgress(*epub, getCurrentPosition());
  const ProgressRange pageRange = getPageProgressRange(*epub, currentSpineIndex, currentPage, pageCount);

  const size_t bookmarkCountBeforeToggle = cachedBookmarks.size();
  cachedBookmarks.erase(std::remove_if(cachedBookmarks.begin(), cachedBookmarks.end(),
                                       [&](const BookmarkEntry& b) {
                                         return bookmarkMatchesProgress(b, currentSpineIndex, currentPage, pageCount,
                                                                        pageRange);
                                       }),
                        cachedBookmarks.end());
  if (cachedBookmarks.size() != bookmarkCountBeforeToggle) {
    bookmarkRemoved = true;
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
  }

  if (!BookmarkFile::save(epub->getPath(), cachedBookmarks)) {
    LOG_ERR("ERS", "Failed to save bookmarks");
  }
  requestUpdate();
}
