#include "EpubReaderActivity.h"

#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <NearbyPositionReceive.h>
#include <esp_random.h>
#include <esp_system.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "DictionaryHistoryActivity.h"
#include "DictionaryWordSelectActivity.h"
#include "EpubReaderBookmarksActivity.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "EpubReaderFootnotesActivity.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "EpubReaderSearchActivity.h"
#include "KOReaderAutoSync.h"
#include "KOReaderSyncActivity.h"
#include "MappedInputManager.h"
#include "NearbyPositionSyncActivity.h"
#include "QuoteSelectActivity.h"
#include "QuotesViewerActivity.h"
#include "ReaderFontSizes.h"
#include "ReaderMargins.h"
#include "ReaderPresetMigration.h"
#include "ReaderPresetStore.h"
#include "ReaderPresetsActivity.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "StealLookActivity.h"
#include "activities/network/NearbyFileTransferActivity.h"
#include "activities/settings/StatusBarSettingsActivity.h"
#include "activities/settings/TextSettingsActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "sleep/SleepPauseToggle.h"
#include "util/BookCacheUtils.h"
#include "util/BookFiling.h"
#include "util/BookProgressFile.h"
#include "util/DeferredFavorite.h"
#include "util/ProgressFile.h"
#include "util/ReadingJournalStore.h"
#include "util/SortesSelection.h"

namespace {
using bookfiling::isInFolder;
using bookfiling::READ_FOLDER;
using bookfiling::RECENTS_FOLDER;
using bookfiling::ROOT_FOLDER;
}  // namespace

void EpubReaderActivity::onEnter() {
  Activity::onEnter();
  // Opening a book is seconds of card work already, so it is a good moment to let any
  // queued favorite renames run alongside it rather than have them wait for the lock.
  // The other drains are this activity's onExit, home, and sleep entry.
  DeferredFavorite::flush();

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
  // Where this book's saved quotes sit, so their underlines can be drawn back in.
  loadQuoteAnchors();
  buildChapterMarks();
  hasPageList_ = epub->hasPageList();

  HalFile f;
  if (!sortesMode && ProgressFile::openForRead("ERS", epub->getCachePath(), f)) {
    uint8_t data[10];
    const int dataSize = f.read(data, sizeof(data));
    // Decoded by the same tested function the position sync uses. The reader keeps a
    // page past the chapter's end (the section clamps it) rather than losing the spine.
    if (const auto saved = nearby_position::decodeCachedPosition(data, dataSize > 0 ? dataSize : 0)) {
      currentSpineIndex = saved->spine;
      nextPageNumber = saved->page;
      if (nextPageNumber == UINT16_MAX) {
        // UINT16_MAX is an in-memory navigation sentinel for "open previous
        // chapter on its last page". It should never be treated as persisted
        // resume state after sleep or reopen.
        LOG_DBG("ERS", "Ignoring stale last-page sentinel from progress cache");
        nextPageNumber = 0;
      }
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = saved->pages;
      cachedVisibleTextOffset = saved->offset;
      LOG_DBG("ERS", "Loaded cache: %d, %d", currentSpineIndex, nextPageNumber);
    }
  }
  // We may want a better condition to detect if we are opening for the first time.
  // This will trigger if the book is re-opened at Chapter 0.
  if (!sortesMode && currentSpineIndex == 0) {
    int textSpineIndex = epub->getSpineIndexForTextReference();
    if (textSpineIndex != 0) {
      currentSpineIndex = textSpineIndex;
      cachedVisibleTextOffset.reset();
      LOG_DBG("ERS", "Opened for first time, navigating to text reference at index %d", textSpineIndex);
    }
  }

  if (sortesMode) {
    const auto count = epub->getSpineItemsCount();
    if (count == 0) {
      onGoHome();
      return;
    }
    currentSpineIndex = sortes::below(count, esp_random);
  } else {
    loadLinkStack();
    journal.clock.touch(millis());
    openedSpine = currentSpineIndex;
    openedPage = nextPageNumber;
  }

  loadCachedBookmarks();

  requestUpdate();
}

// The book becomes the one wake resumes, and joins Recents, only once a page of it has
// been drawn (upstream #3724): a book that cannot be laid out was remembered on entry,
// so every wake reopened it straight into the same error.
void EpubReaderActivity::rememberBookOnceRendered() {
  if (sortesMode || bookRemembered || !pageRendered.load(std::memory_order_acquire)) return;
  bookRemembered = true;
  APP_STATE.openEpubPath = epub->getPath();
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(epub->getPath(), epub->getTitle(), epub->getAuthor());
}

void EpubReaderActivity::onExit() {
  Activity::onExit();
  // The reading font's Kerning and Ligatures switches end with the book.
  renderer.setKerning(0, true);
  renderer.setLigatures(0, true);
  renderer.setCharacterSpacing(0, 0);
  renderer.setTextContrast(0);

  // Keep rebuildable font buffers from pinning the heap between reading sessions.
  if (auto* fontCache = renderer.getFontCacheManager()) {
    fontCache->releaseSdFontCaches();
  }

  // Page turns batch their progress writes, so whatever is still queued has to land here.
  // This is also the sleep path: ActivityManager::goToSleep() replaces this activity, which
  // runs onExit() before the device drops into deep sleep.
  if (!flushQueuedProgress()) {
    LOG_ERR("ERS", "Failed to flush batched reader progress on exit");
  }

  releaseGrayscaleStripScratch();

  // Leaving the book is one of the two moments queued favourite renames run (the
  // other is sleep entry). Fire-and-forget: the worker's directory scans overlap
  // this exit's own card work instead of ever standing between a press and a page.
  DeferredFavorite::flush();

  // The extractor holds a raw pointer to this activity's epub; drop it before
  // the activity (and the shared_ptr) goes away.
  ImageBlock::setExtractor(nullptr, nullptr);

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  if (sortesMode) {
    // Report how this session ended: a Sortes visit is not ordinary reading.
    // This reports session state; it does not override sleep or wake policy.
    APP_STATE.lastSleepFromReader = false;
    section.reset();
    epub.reset();
    return;
  }

  APP_STATE.readerActivityLoadCount = 0;
  // Claim this session's read-order stamp in the same write that clears the crash counter,
  // so marking the book below costs no extra save. Claimed even when the book is about to
  // be deleted: the numbers only have to be ordered, never contiguous.
  const uint32_t readOrder = ++APP_STATE.readOrderCounter;
  APP_STATE.saveToFile();

  if (epub) reading_journal::append(journal, millis(), epub->getTitle().c_str());

  // Update this book's home-list progress badge from the current position. One write
  // per reading session (setProgress skips if unchanged), so no page-turn cost.
  if (epub) {
    const int pct = bookPercent();
    RECENT_BOOKS.setProgress(epub->getPath(), pct);
    // The same number, kept beside the book's own cache so the file browser can badge a
    // book the recents list no longer holds, and stamped with this session's read order so
    // the browser can also sort by it.
    // A book on its way to /read is finished by definition, so it is marked read outright
    // rather than at whatever the last page rounded to. A book about to be erased is
    // skipped: its cache directory goes with it.
    if (!pendingDeleteBook) {
      book_progress::Marker marker;
      marker.percent = pendingReadFolderMove ? 100 : static_cast<uint8_t>(pct);
      marker.readOrder = readOrder;
      book_progress::write(epub->getCachePath(), marker);
    }
  }

  // Leaving after a followed link loses the in-RAM return stack; persist it so the
  // book reopens on the viewed page and Back still returns to the link origin.
  applyLinkLanding();
  if (footnoteDepth > 0 && epub) saveLinkStack();

  // Auto Sync pushes only a book whose page moved since it opened.
  const bool pageMoved =
      currentSpineIndex != openedSpine || (section ? section->currentPage : nextPageNumber) != openedPage;

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

  if (pageMoved && !pendingDeleteBook && !finalPath.empty()) ko_auto_sync::markMoved(finalPath);

  if (pendingDeleteBook && !finalPath.empty()) deleteOpenedBook(finalPath);

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

reader_landing::Pending EpubReaderActivity::landingPending() const {
  // Gathering only: which anchors are set, and the two facts that decide whether the
  // reflow anchors still apply. The precedence between them lives in ReaderLanding.h.
  reader_landing::Pending pending;
  pending.explicitOffset = pendingOffsetJump.has_value();
  pending.resumeOffset = cachedVisibleTextOffset.has_value();
  pending.pageJump = pendingPageJump.has_value();
  pending.fragmentAnchor = !pendingAnchor.empty();
  pending.percentJump = pendingPercentJump;
  pending.paragraphScan = pendingParagraphScan_.has_value();
  pending.ordinalAnchor = pendingOrdinalAnchor_.has_value();
  pending.sortesPage = pendingSortesPage;
  // The reflow anchors name a page in the chapter they were captured in.
  pending.sameSpineAsCapture = currentSpineIndex == cachedSpineIndex;
  // A total saved mid-build is a watermark, not the real count.
  pending.haveCapturedPageCount = cachedChapterTotalPageCount > 0;
  return pending;
}

void EpubReaderActivity::applyOrientation(const uint8_t orientation) {
  // No-op if the selected orientation matches current settings.
  if (SETTINGS.orientation == orientation) {
    return;
  }

  {
    RenderLock lock(*this);
    // Persist the selection so the reader keeps the new orientation on next launch.
    SETTINGS.orientation = orientation;
    SETTINGS.saveToFile();

    // Update renderer orientation to match the new logical coordinate system.
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

    // Rotating re-flows the chapter for a different viewport: the same relayout a font
    // change takes (position and paragraph anchor kept, section dropped, ghost cleanup,
    // underline memo cleared). The capture reads only the section, never the renderer,
    // so running it after the orientation switch is safe.
    dropSectionForRelayout();
  }
}

bool EpubReaderActivity::lightPanelAuxText(char* out, const size_t length) const {
  snprintf(out, length, "%s %u", I18N.get(StrId::STR_TEXT_SIZE), static_cast<unsigned>(prefs_.fontPointSize));
  return true;
}

bool EpubReaderActivity::lightPanelStepAux(const int delta) {
  const auto sizes = readerFontPointSizes(&sdFontSystem.registry(), prefs_.sdFontFamilyName);
  if (sizes.size() < 2) return false;  // one installed size: the row is a readout

  const uint8_t current = snapToNearestPointSize(sizes, prefs_.fontPointSize);
  int index = 0;
  for (size_t i = 0; i < sizes.size(); ++i) {
    if (sizes[i] == current) index = static_cast<int>(i);
  }
  const int next = std::clamp(index + delta, 0, static_cast<int>(sizes.size()) - 1);
  if (sizes[next] == prefs_.fontPointSize) return false;

  prefs_.fontPointSize = sizes[next];
  // Per-book, like every other change made from inside the book: the global size belongs
  // to Settings > Reader.
  prefsCustom_ = true;
  writeReaderOverride(prefs_);
  sdFontSystem.ensureLoadedFor(renderer, prefs_.sdFontFamilyName, prefs_.fontPointSize);
  reloadForReaderPrefsChange();
  requestUpdate();
  return true;
}

uint8_t* EpubReaderActivity::acquireGrayscaleStripScratch(const size_t bytes) {
  if (grayscaleStripScratch && grayscaleStripScratchBytes >= bytes) {
    return grayscaleStripScratch.get();
  }
  // Orientation or panel geometry changed: drop the old block first so the new request is not
  // asked for on top of it.
  grayscaleStripScratch.reset();
  grayscaleStripScratchBytes = 0;
  grayscaleStripScratch = makeUniqueNoThrow<uint8_t[]>(bytes);
  if (!grayscaleStripScratch) {
    return nullptr;
  }
  grayscaleStripScratchBytes = bytes;
  return grayscaleStripScratch.get();
}

void EpubReaderActivity::releaseGrayscaleStripScratch() {
  grayscaleStripScratch.reset();
  grayscaleStripScratchBytes = 0;
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
    info.progressPercent = bookPercent();
  }
  return info;
}

std::optional<uint16_t> EpubReaderActivity::visibleParagraphIndex() const {
  const int currentPage = section ? section->currentPage : nextPageNumber;
  if (!section || currentPage < 0 || currentPage >= section->pageCount) return std::nullopt;
  return section->getParagraphIndexForPage(static_cast<uint16_t>(currentPage));
}

CrossPointPosition EpubReaderActivity::getCurrentPosition() const {
  const int currentPage = section ? section->currentPage : nextPageNumber;
  const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
  const std::optional<uint16_t> paragraphIndex = visibleParagraphIndex();

  CrossPointPosition localPos = {currentSpineIndex, currentPage, totalPages};
  localPos.hasResolvedSpineIndex = true;
  localPos.hasMappedPage = true;
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
