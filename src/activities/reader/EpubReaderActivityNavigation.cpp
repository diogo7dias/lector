#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <limits>

#include "DictionaryWordSelectActivity.h"
#include "EpubReaderActivity.h"
#include "EpubReaderSearchActivity.h"
#include "EpubReaderUtils.h"
#include "QueuedTurns.h"
#include "QuoteSelectActivity.h"
#include "ReaderPresetStore.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "util/BookProgressFile.h"

namespace {
int clampPercent(int percent) {
  if (percent < 0) {
    return 0;
  }
  if (percent > 100) {
    return 100;
  }
  return percent;
}
}  // namespace

void EpubReaderActivity::recordJumpOrigin(const bool accepted) {
  if (currentPageSpineIndex >= 0 && currentPageVisibleOffset) {
    returnHistory.recordJump({currentPageSpineIndex, *currentPageVisibleOffset}, accepted);
  }
  if (!accepted) return;
  clearLinkStack();
  returnHistory.finishReturn(false);
  pendingOrdinalAnchor_.reset();
  pendingParagraphScan_.reset();
  pendingPageJump.reset();
  pendingAnchor.clear();
  pendingPercentJump = false;
  pendingOffsetJump.reset();
}

void EpubReaderActivity::jumpToContentOffset(const int spineIndex, const uint32_t offset, const int pageHint) {
  // Caller holds RenderLock. Bookmarks and Return share the same incremental landing path.
  clearLinkStack();
  clearDeferredReposition();
  pendingOrdinalAnchor_.reset();
  pendingParagraphScan_.reset();
  pendingPageJump.reset();
  pendingAnchor.clear();
  pendingPercentJump = false;
  pendingOffsetJump = offset;
  nextPageNumber = std::max(0, pageHint);
  if (section && currentSpineIndex == spineIndex) {
    if (const auto page = section->getPageForVisibleTextOffset(offset)) {
      section->currentPage = *page;
      return;
    }
  }
  currentSpineIndex = spineIndex;
  section.reset();
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

  // The spine item containing the absolute position.
  const int found = epub->getSpineIndexForSize(targetSize);
  if (found >= 0) {
    targetSpineIndex = found;
    prevCumulative = (found > 0) ? epub->getCumulativeSpineItemSize(found - 1) : 0;
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
    recordJumpOrigin();
    clearDeferredReposition();
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    pendingPercentJump = true;
    section.reset();
  }
}

void EpubReaderActivity::pageTurn(bool isForwardTurn, const bool repaint) {
  // A chapter that failed to load (or was dropped for a relayout) leaves no section. Bound
  // long-press actions reach here without the loop's check, so retry the render as it does.
  if (!section) {
    requestUpdate();
    return;
  }
  // Nothing precedes the first page: skip the state resets and the e-ink refresh
  // that a no-op "back" would otherwise trigger.
  if (!isForwardTurn && currentSpineIndex == 0 && section->currentPage == 0) return;

  // A page turn is authoritative: do not let a resume/reflow position captured
  // at session start snap the reader back after the incremental build completes.
  // One lock for the whole turn: the render task reads currentPage and may be mid-render
  // when a spine change drops the section.
  RenderLock lock(*this);
  if (!sortesMode && isForwardTurn && sessionPages < std::numeric_limits<int>::max()) ++sessionPages;
  if (!sortesMode) {
    pageTimer.onTurn(millis(), isForwardTurn);
    journal.turn(millis(), isForwardTurn);
  }
  returnHistory.finishReturn(false);
  pendingOffsetJump.reset();
  clearDeferredReposition();
  // The relayout's paragraph anchor too: kept, it would snap the reader back to that
  // paragraph when the background build finishes, undoing these turns.
  pendingOrdinalAnchor_.reset();

  if (isForwardTurn) {
    // Advance within the section while there are (or may still be) more pages: either a built
    // page ahead, or the section is still building (windowed), in which case more pages exist
    // beyond the current watermark and render()'s ensure-built pump will lay them out. Only when
    // the section is fully built AND we're on its last page do we move to the next spine -- using
    // the live pageCount alone would mistake the build watermark for the end of a giant spine.
    // A partial cache that is not building (its extension failed to start) also has more
    // pages: render() extends it with a blocking build when the turn crosses its watermark.
    if (section->currentPage < section->pageCount - 1 || section->isBuilding() || section->isPartial()) {
      section->currentPage++;
    } else {
      nextPageNumber = 0;
      currentSpineIndex++;
      section.reset();
    }
  } else {
    if (section->currentPage > 0) {
      section->currentPage--;
    } else if (currentSpineIndex > 0) {
      nextPageNumber = 0;
      pendingPageJump = std::numeric_limits<uint16_t>::max();
      currentSpineIndex--;
      section.reset();
    }
  }
  lock.unlock();
  if (repaint) requestUpdate();
}

void EpubReaderActivity::applyQueuedTurns() {
  // While a refresh holds the render lock, pageTurn would block the loop and the presses made
  // meanwhile would never be polled; they wait in queuedTurns until the panel is free.
  if (queuedTurns == 0 || RenderLock::peek()) return;
  const bool forward = queuedTurns > 0;
  const int queued = forward ? queuedTurns : -queuedTurns;
  queuedTurns = 0;
  if (!section) {
    requestUpdate();
    return;
  }
  const int turns = queuedTurnsNow(queued, forward, section->currentPage, section->pageCount);
  for (int i = 0; i < turns && section; ++i) pageTurn(forward, false);
  requestUpdate();
}

bool EpubReaderActivity::applyDeferredReposition() {
  const reader_landing::Pending pending = landingPending();
  // Nothing pending at all, or the chapter cannot answer yet: no decision to make.
  if ((!pending.resumeOffset && !pending.haveCapturedPageCount && !pending.ordinalAnchor) || !section ||
      section->isBuilding()) {
    return false;
  }
  // WHICH anchor wins here is the same decision render() makes, named in ReaderLanding.h.
  const reader_landing::Anchor resolved = reader_landing::forDeferredReposition(pending);
  if (resolved == reader_landing::Anchor::None) {
    // Pending, but captured in a chapter the reader has since left: the anchors name a
    // page that no longer exists here, so drop them rather than resolve them against
    // the wrong chapter.
    clearDeferredReposition();
    return false;
  }

  // The paragraph anchor outranks both fallbacks: it is the place the reader actually
  // was. The chapter is fully laid out by now, so the paragraph is certainly in it.
  if (resolved == reader_landing::Anchor::OrdinalAnchor && section->pageCount > 0) {
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
  // Older 4/6-byte progress files retain the page-fraction fallback. The spine check the
  // old code repeated here is already in forDeferredReposition: reaching this point means
  // the anchors belong to this chapter.
  {
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
  clearDeferredReposition();
  return changed;
}

void EpubReaderActivity::clearDeferredReposition() {
  cachedChapterTotalPageCount = 0;
  cachedVisibleTextOffset.reset();
}

void EpubReaderActivity::navigateToHref(const std::string& hrefStr, const bool savePosition) {
  if (!epub) return;

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
    targetSpineIndex = epub->resolveHrefToSpineIndex(hrefStr, currentSpineIndex);
  }

  if (targetSpineIndex < 0) {
    LOG_DBG("ERS", "Could not resolve href: %s", hrefStr.c_str());
    return;
  }

  {
    RenderLock lock(*this);
    // The previous link's landing may have been published after this pass applied it.
    applyLinkLanding();
    if (savePosition) {
      // A child screen (menu, footnote list) may have released the section;
      // nextPageNumber then holds the page it was on.
      const int page = section ? section->currentPage : nextPageNumber;
      link_stack::push(savedPositions, footnoteDepth, MAX_FOOTNOTE_DEPTH, {currentSpineIndex, page});
      LOG_DBG("ERS", "Saved position [%d]: spine %d, page %d", footnoteDepth, currentSpineIndex, page);
    }
    cancelLinkLanding();
    linkLandingPending = savePosition;
    returnHistory.finishReturn(false);
    pendingOffsetJump.reset();
    clearDeferredReposition();
    pendingAnchor = std::move(anchor);
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    section.reset();
  }
  requestUpdate();
  LOG_DBG("ERS", "Navigated to spine %d for href: %s", targetSpineIndex, hrefStr.c_str());
}

void EpubReaderActivity::saveLinkStack() const {
  // [depth] then depth x (spine u16 LE, page u16 LE)
  uint8_t data[1 + MAX_FOOTNOTE_DEPTH * 4];
  size_t size = 0;
  data[size++] = static_cast<uint8_t>(footnoteDepth);
  for (int i = 0; i < footnoteDepth; i++) {
    const SavedPosition& pos = savedPositions[i];
    data[size++] = pos.spineIndex & 0xFF;
    data[size++] = (pos.spineIndex >> 8) & 0xFF;
    data[size++] = pos.pageNumber & 0xFF;
    data[size++] = (pos.pageNumber >> 8) & 0xFF;
  }
  HalFile f;
  if (!Storage.openFileForWrite("ERS", EpubReaderUtils::linkStackPath(epub->getCachePath()), f)) return;
  if (f.write(data, size) != size) LOG_ERR("ERS", "Failed to write link stack");
}

void EpubReaderActivity::loadLinkStack() {
  const std::string path = EpubReaderUtils::linkStackPath(epub->getCachePath());
  if (!Storage.exists(path.c_str())) return;
  {
    HalFile f;
    uint8_t data[1 + MAX_FOOTNOTE_DEPTH * 4];
    if (Storage.openFileForRead("ERS", path, f)) {
      const int size = f.read(data, sizeof(data));
      const int depth = size > 0 ? data[0] : 0;
      if (depth >= 1 && depth <= MAX_FOOTNOTE_DEPTH && size == 1 + depth * 4) {
        const int spineCount = epub->getSpineItemsCount();
        bool valid = true;
        for (int i = 0; i < depth; i++) {
          const uint8_t* p = data + 1 + i * 4;
          savedPositions[i] = {p[0] | (p[1] << 8), p[2] | (p[3] << 8)};
          valid = valid && savedPositions[i].spineIndex < spineCount;
        }
        if (valid) {
          footnoteDepth = depth;
          LOG_DBG("ERS", "Loaded link stack, depth %d", depth);
        }
      }
    }
  }
  // Consumed once: a later exit rewrites it, and an unclean shutdown must not
  // resurrect a stale stack.
  Storage.remove(path.c_str());
}

void EpubReaderActivity::applyLinkLanding() {
  if (!linkLandingReady.exchange(false, std::memory_order_acq_rel)) return;
  footnoteDepth = link_stack::depthAfterLanding(savedPositions, footnoteDepth, linkLanding);
}

void EpubReaderActivity::cancelLinkLanding() {
  linkLandingPending = false;
  linkLandingReady.store(false, std::memory_order_relaxed);
}

void EpubReaderActivity::clearLinkStack() {
  footnoteDepth = 0;
  cancelLinkLanding();
}

void EpubReaderActivity::restoreSavedPosition() {
  {
    RenderLock lock(*this);
    applyLinkLanding();
    if (footnoteDepth <= 0) return;
    footnoteDepth--;
    const auto& pos = savedPositions[footnoteDepth];
    LOG_DBG("ERS", "Restoring position [%d]: spine %d, page %d", footnoteDepth, pos.spineIndex, pos.pageNumber);
    cancelLinkLanding();
    returnHistory.finishReturn(false);
    pendingOffsetJump.reset();
    clearDeferredReposition();
    currentSpineIndex = pos.spineIndex;
    nextPageNumber = pos.pageNumber;
    section.reset();
  }
  requestUpdate();
}
