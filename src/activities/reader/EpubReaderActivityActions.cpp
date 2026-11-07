#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <FontCacheManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <NearbyPositionReceive.h>
#include <esp_system.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "DictionaryHistoryActivity.h"
#include "DictionaryWordSelectActivity.h"
#include "EpubReaderActivity.h"
#include "EpubReaderBookmarksActivity.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "EpubReaderFootnotesActivity.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "EpubReaderSearchActivity.h"
#include "EpubReaderUtils.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncActivity.h"
#include "NearbyPositionSyncActivity.h"
#include "ProgressMapper.h"
#include "QuoteSelectActivity.h"
#include "QuotesViewerActivity.h"
#include "ReaderMargins.h"
#include "ReaderPresetStore.h"
#include "ReaderPresetsActivity.h"
#include "ReaderUtils.h"
#include "StealLookActivity.h"
#include "activities/network/NearbyFileTransferActivity.h"
#include "activities/settings/StatusBarSettingsActivity.h"
#include "activities/settings/TextSettingsActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "sleep/SleepPauseToggle.h"
#include "util/BookFiling.h"
#include "util/BookProgressFile.h"
#include "util/DeferredFavorite.h"
#include "util/FavoriteImage.h"
#include "util/SortesSelection.h"

void EpubReaderActivity::toggleWallpaperFavorite() {
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
  if (lastPath.empty()) return;
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
}

void EpubReaderActivity::toggleWallpaperHold() {
  ReaderUtils::toggleWallpaperHold(renderer, pagesUntilFullRefresh);
  requestUpdate();
}

void EpubReaderActivity::toggleWallpaperPause() {
  // A queued favourite rename may still own this file's name on the card
  // (renames run at flush points, not at the press). Land it first so the
  // move below acts on the name APP_STATE promises. Rare action, and it does
  // a synchronous rename itself, so the wait is in character here.
  DeferredFavorite::waitForIdle(15000);
  DeferredFavorite::reconcile();
  const std::string lastPath = APP_STATE.lastSleepWallpaperPath;
  if (lastPath.empty()) return;
  const auto moved = crosspoint::sleep::toggleSleepPause(lastPath);
  if (!moved.ok) {
    GUI.drawPopup(renderer, tr(STR_MOVE_FAILED));
    scheduleGhostCleanup();
  } else {
    GUI.drawPopup(renderer, moved.toPause ? tr(STR_WALLPAPER_PAUSED) : tr(STR_WALLPAPER_UNPAUSED));
    scheduleGhostCleanup();
  }
  requestUpdate();
}

void EpubReaderActivity::confirmWallpaperDelete() {
  // Same rule as WALLPAPER_PAUSE: land any queued rename so the delete hits
  // the file under its current on-card name.
  DeferredFavorite::waitForIdle(15000);
  DeferredFavorite::reconcile();
  const std::string lastPath = APP_STATE.lastSleepWallpaperPath;
  if (lastPath.empty()) return;
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
        // Clear references to the deleted wallpaper so no later view reopens it.
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
}

void EpubReaderActivity::confirmResetReaderSettings() {
  // Throwing away this book's own font, margins and spacing cannot be undone -- the
  // override file is deleted, not remembered -- so it asks first, the same as
  // deleting the book does. The book is named in the prompt because the menu row
  // itself does not say whose look is about to go.
  const std::string name = epub ? bookfiling::displayNameFor(epub->getTitle(), epub->getPath()) : std::string{};
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput,
                                                                tr(STR_RESET_READER_SETTINGS) + std::string("?"), name),
                         [this](const ActivityResult& res) {
                           if (res.isCancelled) {
                             requestUpdate();
                             return;
                           }
                           resetReaderPrefsToGlobal();
                         });
}

void EpubReaderActivity::removeFromRecents() {
  // The file move and the list edit both happen in onExit, where the Epub is
  // already released — renaming a book with an open handle is what the /read and
  // /recents filing carefully avoids, and this is the same move in reverse.
  pendingRemoveFromRecents = true;
  onGoHome();
}

void EpubReaderActivity::confirmDeleteBook() {
  if (!epub) return;
  const std::string path = epub->getPath();
  // The book's own title reads better in the prompt than its file name; a book
  // whose metadata carries no usable title falls back to the name on the card.
  const std::string name = bookfiling::displayNameFor(epub->getTitle(), path);
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
}

void EpubReaderActivity::deleteCacheAndGoHome() {
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
}

bool EpubReaderActivity::blockSortesAction() {
  if (!sortesMode) return false;
  auto message =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_SORTES), tr(STR_SORTES_OPEN_NORMALLY));
  if (!message) {
    LOG_ERR("SORTES", "OOM: unavailable action message");
    return true;
  }
  startActivityForResult(std::move(message), [this](const ActivityResult&) { requestUpdate(); });
  return true;
}

bool EpubReaderActivity::saveProgressForHandoff(const char* tag, const int currentPage, const int totalPages) {
  if (saveProgress(currentSpineIndex, currentPage, totalPages)) return true;
  LOG_ERR(tag, "Aborting handoff because current progress could not be saved");
  pendingSyncSaveError = true;
  requestUpdate();
  return false;
}

void EpubReaderActivity::releaseBookForHandoff() {
  RenderLock lock(*this);
  if (section) {
    nextPageNumber = section->currentPage;
  }
  // The image extractor holds a raw pointer into this epub (see onEnter);
  // clear it before the early release, mirroring onExit(), or a later image
  // render would call through a dangling context.
  ImageBlock::setExtractor(nullptr, nullptr);
  section.reset();
  // onExit() can no longer save the back-stack once epub is gone.
  if (footnoteDepth > 0 && epub) saveLinkStack();
  epub.reset();
}

void EpubReaderActivity::launchNearbyBookSend() {
  if (blockSortesAction()) return;
  const int currentPage = section ? section->currentPage : nextPageNumber;
  const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
  const std::string savedEpubPath = epub->getPath();

  // The transfer screen reopens this book on the way out, and goToReader() reads
  // this file to know where to land, so a failed write aborts the send.
  if (!saveProgressForHandoff("NBFT", currentPage, totalPages)) return;

  // Release Epub and Section before the radio comes up, as the sync paths do.
  releaseBookForHandoff();

  activityManager.replaceActivity(std::make_unique<NearbyFileTransferActivity>(
      renderer, mappedInput, NearbyFileTransferActivity::Mode::Send, savedEpubPath, savedEpubPath));
}

void EpubReaderActivity::launchNearbyPositionSync() {
  if (blockSortesAction()) return;
  const int currentPage = section ? section->currentPage : nextPageNumber;
  const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
  const std::optional<uint16_t> paragraphIndex = visibleParagraphIndex();

  // Computed while the Epub is still in RAM, exactly as the KOSync path does:
  // the position travels as an xpath, so it must be resolved before the book is
  // released.
  CrossPointPosition localPos = getCurrentPosition();
  SavedProgressPosition localKoPos = ProgressMapper::toSavedProgress(*epub, localPos);
  const std::string savedEpubPath = epub->getPath();

  // goToReader() on the way back reads this file, so a failed write aborts the
  // sync rather than risking a return to the wrong page.
  if (!saveProgressForHandoff("NBPS", currentPage, totalPages)) return;

  // Release Epub and Section before the radio comes up: ESP-NOW needs less heap
  // than the TLS handshake KOSync makes room for, but it still needs room.
  releaseBookForHandoff();

  activityManager.replaceActivity(
      std::make_unique<NearbyPositionSyncActivity>(renderer, mappedInput, savedEpubPath, currentSpineIndex, currentPage,
                                                   totalPages, std::move(localKoPos), paragraphIndex));
}

bool EpubReaderActivity::launchKOReaderSync(const bool autoMode) {
  if (blockSortesAction()) return true;
  if (!KOREADER_STORE.hasCredentials()) return false;  // no-op: nothing to launch

  const int currentPage = section ? section->currentPage : nextPageNumber;
  const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;

  // Pre-compute local KO position and chapter name while Epub is still in RAM.
  CrossPointPosition localPos = getCurrentPosition();
  SavedProgressPosition localKoPos = ProgressMapper::toSavedProgress(*epub, localPos);
  const int tocIdx = epub->getTocIndexForSpineIndex(currentSpineIndex);
  std::string localChapterName = (tocIdx >= 0) ? epub->getTocItem(tocIdx).title : "";
  const std::string savedEpubPath = epub->getPath();

  // Persist current position so the reader resumes at the right page on return.
  // goToReader() depends on this file, so abort the sync if the write fails.
  if (!saveProgressForHandoff("KOSync", currentPage, totalPages)) {
    return true;  // acted: surfaced a save error to the user
  }

  // Release Epub and Section to free ~65KB RAM for the TLS handshake.
  LOG_DBG("KOSync", "Releasing epub for sync (heap before: %u)", (unsigned)ESP.getFreeHeap());
  releaseBookForHandoff();
  LOG_DBG("KOSync", "Epub released (heap after: %u)", (unsigned)ESP.getFreeHeap());

  activityManager.replaceActivity(std::make_unique<KOReaderSyncActivity>(
      renderer, mappedInput, savedEpubPath, localPos, std::move(localKoPos), std::move(localChapterName), autoMode));
  return true;  // acted: launched the sync activity
}
