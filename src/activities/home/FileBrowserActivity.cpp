#include "FileBrowserActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <esp_random.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <iterator>

#include "CrossPointSettings.h"
#include "ImageViewerPatch.h"
#include "LargeFolderIndexPolicy.h"
#include "LargeFolderLoadPolicy.h"
#include "LibrarySearchSupport.h"
#include "MappedInputManager.h"
#include "activities/util/BmpViewerActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "activities/util/PxcViewerActivity.h"
#include "components/ListWindowRefresh.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"
#include "util/FavoriteImage.h"

namespace {
constexpr unsigned long GO_HOME_MS = 1000;
constexpr size_t NAME_BUFFER_SIZE = 500;
}  // namespace

// Defined below; forward-declared so openSearch()'s preview lambda can use it.
std::string getFileName(std::string filename);
// List label with a favorite marker: defined below, forward-declared for the
// preview lambda and the row-name callback.
std::string favoriteAwareFileName(const std::string& filename);

bool FileBrowserActivity::loadFiles(const SdFileIndex::CancelFn& cancel) {
  files.clear();
  sdIndex.clear();
  sdMode = false;
  folderHasBooks_ = false;
  bool cancelled = false;
  const auto cancelRequested = [&] {
    if (!cancel || !cancel()) return false;
    cancelled = true;
    return true;
  };

  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "fileNameBuffer not allocated");
    return true;
  }

  auto accept = [this](const char* name, const bool isDirectory) {
    if ((!SETTINGS.showHiddenFiles && name[0] == '.') || strcmp(name, "System Volume Information") == 0) return false;
    if (isDirectory) return true;
    const std::string_view filename{name};
    if (mode == Mode::PickFirmware) return FsHelpers::checkFileExtension(filename, ".bin");

    const bool wallpaper = SETTINGS.wallpaperFormat == CrossPointSettings::WALLPAPER_PXC
                               ? FsHelpers::checkFileExtension(filename, ".pxc")
                               : FsHelpers::hasBmpExtension(filename);
    const bool book = FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
                      FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename);
    if (book) folderHasBooks_ = true;
    return book || wallpaper;
  };

  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) return true;

  // Pre-size for a typical folder so the first handful of push_backs do not each
  // trigger a vector re-grow (allocate + copy + free — three heap ops that also
  // fragment DRAM). Large folders bail to the SD index well before this matters.
  files.reserve(64);

  root.rewindDirectory();
  size_t retainedNameBytes = 0;
  bool needsSdIndex = false;
  size_t scanned = 0;
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    if (cancelRequested()) {
      root.close();
      files.clear();
      return false;
    }
    // Feed the watchdog and yield periodically: a folder can hold up to ~1024
    // entries before the SD-index cutover, and the plain scan otherwise holds the
    // task without a break, starving the UI/repaint and risking a WDT trip.
    if ((++scanned % 64) == 0) {
      esp_task_wdt_reset();
      yield();
    }
    file.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
    const bool isDirectory = file.isDirectory();
    if (!accept(fileNameBuffer.get(), isDirectory)) continue;
    std::string displayName = fileNameBuffer.get();
    if (isDirectory) displayName.push_back('/');
    retainedNameBytes += displayName.size();
    files.push_back(std::move(displayName));
    if (large_folder_index::shouldUseSdIndex(files.size(), retainedNameBytes)) {
      needsSdIndex = true;
      break;
    }
  }
  root.close();

  if (needsSdIndex) {
    std::vector<std::string>().swap(files);
    folderHasBooks_ = false;
    // Only large folders reach here, and the SD-index build below (an external
    // merge sort over hundreds/thousands of 500-byte records) can take seconds.
    // Paint the "Opening folder..." top strip now — synchronously, so it is on
    // the panel before we block — so the user gets feedback during the wait.
    // Navigating into a subfolder does not otherwise repaint any banner (only
    // the first browser entry does, via ActivityManager::goToFileBrowser), so
    // without this a slow /sleep pause open just looks frozen. Fast folders skip
    // the SD index and never flash this strip.
    GUI.drawPopup(renderer, tr(STR_BANNER_OPENING_FOLDER));
    if (sdIndex.build(basepath, accept, cancelRequested)) {
      sdMode = true;
      if (SETTINGS.bookBrowserRandomOrder && mode == Mode::Books) sdIndex.shuffleTail();
      return true;
    }
    if (cancelled) return false;
    LOG_ERR("FileBrowser", "SD index failed; folder list unavailable");
    folderHasBooks_ = false;
  }

  FsHelpers::sortFileList(files);

  // Random book order (Books mode only): reshuffle the file entries so the user
  // gets a fresh random ordering each time the folder loads, to help pick the next
  // read. Directories stay grouped and sorted at the top (sortFileList puts them
  // first) — only the non-directory tail is shuffled (Fisher-Yates via esp_random).
  if (SETTINGS.bookBrowserRandomOrder && mode == Mode::Books) {
    size_t firstFile = 0;
    while (firstFile < files.size() && !files[firstFile].empty() && files[firstFile].back() == '/') {
      firstFile++;
    }
    for (size_t i = files.size(); i > firstFile + 1; i--) {
      const size_t j = firstFile + esp_random() % (i - firstFile);
      std::swap(files[i - 1], files[j]);
    }
  }
  return true;
}

ActivityResultHandler FileBrowserActivity::imageViewerResultHandler(const std::string& launchName,
                                                                    const size_t launchFileIndex) {
  return [this, launchName, launchFileIndex](const ActivityResult& res) {
    if (!std::holds_alternative<FilePathResult>(res.data)) return;
    const auto& result = std::get<FilePathResult>(res.data);
    const auto patch = image_viewer_patch::plan(result.path, result.sourcePath);
    if (sdMode) {
      bool patched = patch.valid;
      size_t patchIndex = launchFileIndex;
      if (patched && patch.action != image_viewer_patch::Action::None && patch.sourceName != launchName) {
        patchIndex = sdIndex.lowerBound(patch.sourceName);
        patched = patchIndex < sdIndex.count() && sdIndex.nameAt(patchIndex) == patch.sourceName;
      }
      if (patched && patch.action == image_viewer_patch::Action::Erase) {
        const auto targetRow = image_viewer_patch::selectorForSource(patchIndex, static_cast<size_t>(headerRowCount()),
                                                                     searchActive() ? &filteredIndexes : nullptr);
        if (targetRow) selectorIndex = *targetRow;
        patched = sdIndex.eraseAt(patchIndex);
      }
      if (patched && patch.action == image_viewer_patch::Action::Rename)
        patched = sdIndex.renameAt(patchIndex, patch.finalName);
      if (!patched) loadFiles();
      rebuildFilter();
      const size_t rows = totalRowCount();
      if (rows == 0) {
        selectorIndex = 0;
      } else if (selectorIndex >= rows) {
        selectorIndex = rows - 1;
      }
      pendingFullRefresh = true;
      requestUpdate(true);
      return;
    }
    if (patch.action == image_viewer_patch::Action::Erase) {
      // Moved or deleted in the viewer: drop the row in place — no folder rescan.
      const auto source = std::find(files.begin(), files.end(), patch.sourceName);
      if (source != files.end()) {
        const size_t sourceIndex = static_cast<size_t>(std::distance(files.begin(), source));
        const auto targetRow = image_viewer_patch::selectorForSource(sourceIndex, static_cast<size_t>(headerRowCount()),
                                                                     searchActive() ? &filteredIndexes : nullptr);
        if (targetRow) selectorIndex = *targetRow;
        files.erase(source);
      }
    } else if (patch.action == image_viewer_patch::Action::Rename) {
      // Still present, possibly favorite-renamed (_F): update the row and re-sort
      // (RAM-only, cheap even at thousands of entries — the SD scan is what's slow).
      for (auto& f : files) {
        if (f == patch.sourceName) {
          f = patch.finalName;
          break;
        }
      }
      FsHelpers::sortFileList(files);
    }
    rebuildFilter();  // `files` changed — refresh the ranked/search view
    const int postRowCount = static_cast<int>(totalRowCount());
    if (postRowCount == 0) {
      selectorIndex = 0;
    } else if (selectorIndex >= static_cast<size_t>(postRowCount)) {
      selectorIndex = postRowCount - 1;
    }
    // The viewer just painted a full-page grayscale image; scrub it with a full
    // refresh so it does not ghost behind the list (worst during bulk moves).
    pendingFullRefresh = true;
    requestUpdate(true);
  };
}

void FileBrowserActivity::openPxcViewer(const std::string& path, const std::string& launchName,
                                        const size_t launchFileIndex) {
  startActivityForResult(makeUniqueNoThrow<PxcViewerActivity>(renderer, mappedInput, path, /*resultMode=*/true),
                         imageViewerResultHandler(launchName, launchFileIndex));
}

void FileBrowserActivity::openBmpViewer(const std::string& path, const std::string& launchName,
                                        const size_t launchFileIndex) {
  startActivityForResult(makeUniqueNoThrow<BmpViewerActivity>(renderer, mappedInput, path, /*resultMode=*/true),
                         imageViewerResultHandler(launchName, launchFileIndex));
}

// Map a selector row index to what it represents: a synthetic action row (Recent
// Books / Search / Clear search) or a real file. File rows read through the ranked
// filteredIndexes while a search is active.
FileBrowserActivity::RowRef FileBrowserActivity::rowAt(size_t rowIndex) const {
  size_t i = rowIndex;
  if (hasRecentShortcut()) {
    if (i == 0) return {RowKind::Recent};
    --i;
  }
  if (folderHasBooks_) {
    if (i == 0) return {RowKind::Search};
    --i;
    if (searchActive()) {
      if (i == 0) return {RowKind::Clear};
      --i;
    }
  }
  size_t fileIdx = i;
  if (searchActive()) {
    fileIdx = (i < filteredIndexes.size()) ? filteredIndexes[i] : 0;
  }
  return {RowKind::File, fileIdx};
}

void FileBrowserActivity::rebuildFilter() {
  if (activeSearchQuery.empty()) {
    filteredIndexes.clear();
    return;
  }
  if (sdMode) {
    filteredIndexes = LibrarySearchSupport::rankMatches(
        sdIndex.count(), [this](const size_t index) { return sdIndex.nameAt(index); }, activeSearchQuery,
        large_folder_index::MAX_SEARCH_RESULTS);
  } else {
    filteredIndexes = LibrarySearchSupport::rankMatches(files, activeSearchQuery);
  }
}

void FileBrowserActivity::clearSearch() {
  activeSearchQuery.clear();
  filteredIndexes.clear();
}

void FileBrowserActivity::openSearch() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH), activeSearchQuery,
                                                           /*maxLength=*/64, InputType::Text);
  if (!keyboard) {
    LOG_ERR("FILE", "OOM: search keyboard");
    return;
  }
  // Live preview: re-rank `files` on every keystroke and show the top matches on the
  // keyboard screen (same ranker as the applied filter, so preview == result).
  keyboard->setLivePreview([this](const std::string& query, int maxRows) -> KeyboardEntryActivity::KbPreviewResult {
    KeyboardEntryActivity::KbPreviewResult out;
    if (query.empty()) return out;
    const auto ranked = sdMode ? LibrarySearchSupport::rankMatches(
                                     sdIndex.count(), [this](const size_t index) { return sdIndex.nameAt(index); },
                                     query, large_folder_index::MAX_SEARCH_RESULTS)
                               : LibrarySearchSupport::rankMatches(files, query);
    out.total = static_cast<int>(ranked.size());
    for (int i = 0; i < static_cast<int>(ranked.size()) && i < maxRows; i++) {
      out.rows.push_back(favoriteAwareFileName(fileNameAt(ranked[i])));
    }
    return out;
  });
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& res) {
    if (!res.isCancelled) {
      activeSearchQuery = std::get<KeyboardResult>(res.data).text;
      rebuildFilter();
      // Land on the first result (or the last header row when nothing matched).
      const size_t total = totalRowCount();
      selectorIndex = total == 0 ? 0 : std::min<size_t>(static_cast<size_t>(headerRowCount()), total - 1);
    }
    requestUpdate();
  });
}

void FileBrowserActivity::onEnter() {
  Activity::onEnter();

  fileNameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "malloc failed for name buffer");
    return;
  }

  selectorIndex = 0;

  // If Confirm was held while this activity opened (typical when launched from a menu), ignore
  // its release — otherwise we'd immediately auto-open whatever is at index 0.
  lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);

  auto root = Storage.open(basepath.c_str());
  if (!root) {
    // basepath may be a file that was just moved/deleted (e.g. a wallpaper sent to
    // /sleep pause from the viewer). Re-open its folder and land on the slot it
    // occupied, instead of jumping all the way back to root.
    std::string folder = FsHelpers::extractFolderPath(basepath);
    if (folder.empty()) folder = "/";
    auto folderRoot = Storage.open(folder.c_str());
    if (folderRoot && folderRoot.isDirectory()) {
      const auto pos = basepath.find_last_of('/');
      const std::string fileName = (pos == std::string::npos) ? basepath : basepath.substr(pos + 1);
      basepath = folder;
      loadFiles();
      selectorIndex = findEntry(fileName);
    } else {
      basepath = "/";
      loadFiles();
    }
  } else if (!root.isDirectory()) {
    lockLongPressBack = mappedInput.isPressed(MappedInputManager::Button::Back);

    const std::string oldPath = basepath;
    basepath = FsHelpers::extractFolderPath(basepath);
    loadFiles();

    const auto pos = oldPath.find_last_of('/');
    const std::string fileName = oldPath.substr(pos + 1);
    selectorIndex = findEntry(fileName);
  } else {
    loadFiles();
  }

  pendingFullRefresh = true;  // clean full paint on entry / resume from a child activity
  requestUpdate();
}

void FileBrowserActivity::onExit() {
  Activity::onExit();
  files.clear();
  sdIndex.clear();
  fileNameBuffer.reset();
}

// To avoid traversing directories twice (once for cache clearing, once for deletion),
// we do both in one pass here, instead of using Storage.removeDir
bool FileBrowserActivity::removeDirFile(const std::string& fullPath) {
  auto file = Storage.open(fullPath.c_str());
  if (!file) {
    LOG_ERR("FileBrowser", "Failed to open for metadata clearing: %s", fullPath.c_str());
    return false;
  }

  if (!file.isDirectory()) {
    file.close();
    clearBookCache(fullPath);
    return Storage.remove(fullPath.c_str());
  }
  file.close();

  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "fileNameBuffer not allocated");
    return false;
  }

  // Stack of (dirPath, postOrder): postOrder=true means rmdir this path after children are processed.
  std::vector<std::pair<std::string, bool>> stack;
  stack.reserve(16);
  stack.push_back({fullPath, false});

  while (!stack.empty()) {
    auto [currentPath, postOrder] = std::move(stack.back());
    stack.pop_back();

    if (postOrder) {
      if (!Storage.rmdir(currentPath.c_str())) {
        LOG_ERR("FileBrowser", "Failed to rmdir: %s", currentPath.c_str());
        return false;
      }
      continue;
    }

    auto dir = Storage.open(currentPath.c_str());
    if (!dir) {
      LOG_ERR("FileBrowser", "Failed to open dir: %s", currentPath.c_str());
      return false;
    }
    if (!dir.isDirectory()) {
      LOG_ERR("FileBrowser", "Not a directory: %s", currentPath.c_str());
      return false;
    }

    // Push this dir for post-order rmdir (after all children are processed).
    stack.push_back({currentPath, true});

    dir.rewindDirectory();
    for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      entry.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
      if (strcmp(fileNameBuffer.get(), ".") == 0 || strcmp(fileNameBuffer.get(), "..") == 0) {
        continue;
      }
      std::string entryPath = currentPath;
      if (entryPath.back() != '/') {
        entryPath += "/";
      }
      entryPath += fileNameBuffer.get();

      const bool isDir = entry.isDirectory();
      entry.close();

      if (isDir) {
        stack.push_back({std::move(entryPath), false});
      } else {
        clearBookCache(entryPath);
        if (!Storage.remove(entryPath.c_str())) {
          LOG_ERR("FileBrowser", "Failed to remove file: %s", entryPath.c_str());
          return false;
        }
      }
    }
  }

  return true;
}

void FileBrowserActivity::loop() {
  // Long press BACK (1s+) goes to root folder (Books mode only).
  // In firmware-pick mode we keep navigation simple: short Back = up dir / cancel.
  if (mode == Mode::Books && mappedInput.isPressed(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() >= GO_HOME_MS && basepath != "/" && !lockLongPressBack) {
    basepath = "/";
    clearSearch();
    loadFiles();
    selectorIndex = 0;
    requestUpdate();
    return;
  }

  if (lockLongPressBack && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    lockLongPressBack = false;
    return;
  }

  const int pathReserved = renderer.getLineHeight(SMALL_FONT_ID) + UITheme::getInstance().getMetrics().verticalSpacing;
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false, pathReserved);

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (lockNextConfirmRelease) {
      lockNextConfirmRelease = false;
      return;
    }
    if (totalRowCount() == 0) return;

    const RowRef row = rowAt(selectorIndex);
    // Recent Books shortcut row (root of the book browser): open the recents list.
    if (row.kind == RowKind::Recent) {
      activityManager.goToRecentBooks();
      return;
    }
    // Search row: type/edit a query for this folder.
    if (row.kind == RowKind::Search) {
      openSearch();
      return;
    }
    // Clear-search row: drop the filter, land on the first file row.
    if (row.kind == RowKind::Clear) {
      clearSearch();
      selectorIndex = static_cast<size_t>(headerRowCount());
      requestUpdate();
      return;
    }

    const size_t fileIndex = row.fileIndex;
    const std::string entry = fileNameAt(fileIndex);
    if (entry.empty()) return;
    bool isDirectory = (entry.back() == '/');

    // Firmware picker: select file -> return path; navigate into directories normally.
    if (mode == Mode::PickFirmware && !isDirectory) {
      std::string cleanBasePath = basepath;
      if (cleanBasePath.back() != '/') cleanBasePath += "/";
      ActivityResult res{FilePathResult{cleanBasePath + entry}};
      res.isCancelled = false;
      setResult(std::move(res));
      finish();
      return;
    }

    if (mode == Mode::Books && mappedInput.getHeldTime() >= GO_HOME_MS) {
      // --- LONG PRESS ACTION: DELETE FILE OR DIRECTORY ---
      std::string cleanBasePath = basepath;
      if (cleanBasePath.back() != '/') cleanBasePath += "/";
      const std::string fullPath = cleanBasePath + entry;

      auto handler = [this, fullPath](const ActivityResult& res) {
        if (!res.isCancelled) {
          LOG_DBG("FileBrowser", "Attempting to delete: %s", fullPath.c_str());
          if (removeDirFile(fullPath)) {
            LOG_DBG("FileBrowser", "Deleted successfully");
            loadFiles();
            rebuildFilter();  // `files` changed — refresh the ranked search view
            // Clamp against the full row count (files/filtered rows + synthetic
            // header rows) so deleting the last file lands on a valid row.
            const int postRowCount = static_cast<int>(totalRowCount());
            if (postRowCount == 0) {
              selectorIndex = 0;
            } else if (selectorIndex >= static_cast<size_t>(postRowCount)) {
              // Move selection to the new "last" item
              selectorIndex = postRowCount - 1;
            }

            pendingFullRefresh = true;  // clear the deleted row's ghost
            requestUpdate(true);
          } else {
            LOG_ERR("FileBrowser", "Failed to delete: %s", fullPath.c_str());
          }
        } else {
          LOG_DBG("FileBrowser", "Delete cancelled by user");
        }
      };

      std::string heading = tr(STR_DELETE) + std::string("? ");

      startActivityForResult(makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, heading, entry), handler);
      return;
    } else {
      // --- SHORT PRESS ACTION: OPEN/NAVIGATE ---
      if (basepath.back() != '/') basepath += "/";

      if (isDirectory) {
        const std::string parentPath = large_folder_load::restoredParentPath(basepath);
        basepath += entry.substr(0, entry.length() - 1);
        clearSearch();  // search is scoped to the folder you're standing in
        const auto cancelForBack = [this] {
          mappedInput.update();
          return large_folder_load::shouldCancel(mappedInput.isPressed(MappedInputManager::Button::Back),
                                                 mappedInput.wasReleased(MappedInputManager::Button::Back));
        };
        if (!loadFiles(cancelForBack)) {
          LOG_DBG("FileBrowser", "Folder load cancelled by Back");
          basepath = parentPath;
          loadFiles();
          selectorIndex = findEntry(entry);
          lockLongPressBack = mappedInput.isPressed(MappedInputManager::Button::Back);
          pendingFullRefresh = true;
          requestUpdate(true);
          return;
        }
        selectorIndex = 0;
        requestUpdate();
      } else if (FsHelpers::checkFileExtension(entry, ".pxc")) {
        openPxcViewer(basepath + entry, entry, fileIndex);
      } else if (FsHelpers::hasBmpExtension(entry)) {
        openBmpViewer(basepath + entry, entry, fileIndex);
      } else {
        onSelectBook(basepath + entry);
      }
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Short press: go up one directory, or go home if at root
    if (mappedInput.getHeldTime() < GO_HOME_MS) {
      if (basepath != "/") {
        const std::string oldPath = basepath;

        basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
        if (basepath.empty()) basepath = "/";
        clearSearch();
        loadFiles();

        const auto pos = oldPath.find_last_of('/');
        const std::string dirName = oldPath.substr(pos + 1) + "/";
        selectorIndex = findEntry(dirName);

        requestUpdate();
      } else if (mode == Mode::PickFirmware) {
        // Firmware picker at root: cancel back to caller instead of going home.
        ActivityResult res;
        res.isCancelled = true;
        setResult(std::move(res));
        finish();
      } else {
        onGoHome();
      }
    }
  }

  int listSize = static_cast<int>(totalRowCount());
  buttonNavigator.onNextPress([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousPress([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
}

std::string getFileName(std::string filename) {
  if (filename.back() == '/') {
    filename.pop_back();
    if (!UITheme::getInstance().getTheme().showsFileIcons()) {
      return "[" + filename + "]";
    }
    return filename;
  }
  const auto pos = filename.rfind('.');
  return filename.substr(0, pos);
}

// List label with a leading favorite marker. Favorite images carry an "_F"
// suffix before the extension (x_F.bmp / x_F.pxc); show them as "[F] x" — marker
// added, raw suffix removed — so a favorite stands out at a glance in /sleep,
// /sleep pause, or any wallpaper folder. Favorites are image-only, so books and
// other files are never marked and just get the plain name stem.
std::string favoriteAwareFileName(const std::string& filename) {
  if (FavoriteImage::isFavoritePath(filename)) {
    return "[F] " + getFileName(FavoriteImage::stripFavoriteSuffix(filename));
  }
  return getFileName(filename);
}

std::string getFileExtension(const std::string& filename) {
  if (filename.empty() || filename.back() == '/') {
    return "";
  }
  const auto pos = filename.rfind('.');
  // No dot at all: rfind returns npos, and substr(npos) throws std::out_of_range,
  // which aborts under -fno-exceptions. An extensionless entry has no extension.
  if (pos == std::string::npos) {
    return "";
  }
  return filename.substr(pos);
}

void FileBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  std::string folderName =
      (mode == Mode::PickFirmware)
          ? std::string(tr(STR_SELECT_FIRMWARE_FILE))
          : ((basepath == "/") ? std::string(tr(STR_SD_CARD)) : basepath.substr(basepath.rfind('/') + 1));
  folderName += " (" + std::to_string(fileCount()) + ")";
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str());

  const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int pathReserved = pathLineHeight + metrics.verticalSpacing;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved;
  // Rows = synthetic header rows (Recent Books at SD root, Search, Clear) followed
  // by the file rows (the ranked filtered view while a search is active).
  const size_t rowCount = totalRowCount();
  if (rowCount == 0) {
    const char* emptyMsg = (mode == Mode::PickFirmware) ? tr(STR_NO_BIN_FILES) : tr(STR_NO_FILES_FOUND);
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, emptyMsg);
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, rowCount, selectorIndex,
        [this](int index) -> std::string {
          const RowRef r = rowAt(index);
          switch (r.kind) {
            case RowKind::Recent:
              return tr(STR_MENU_RECENT_BOOKS);
            case RowKind::Search:
              return activeSearchQuery.empty() ? std::string(tr(STR_SEARCH_CURRENT_FOLDER))
                                               : (std::string(tr(STR_EDIT_SEARCH)) + ": " + activeSearchQuery);
            case RowKind::Clear:
              return tr(STR_CLEAR_SEARCH);
            case RowKind::File:
              return favoriteAwareFileName(fileNameAt(r.fileIndex));
          }
          return "";
        },
        nullptr,
        [this](int index) -> UIIcon {
          const RowRef r = rowAt(index);
          switch (r.kind) {
            case RowKind::Recent:
              return Recent;
            case RowKind::Search:
              return Library;
            case RowKind::Clear:
              return None;
            case RowKind::File:
              return UITheme::getFileIcon(fileNameAt(r.fileIndex));
          }
          return None;
        },
        [this](int index) -> std::string {
          const RowRef r = rowAt(index);
          return r.kind == RowKind::File ? getFileExtension(fileNameAt(r.fileIndex)) : std::string("");
        },
        false);
  }

  // Full path display
  {
    const int pathY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - pathLineHeight;
    const int separatorY = pathY - metrics.verticalSpacing / 2;
    renderer.drawLine(0, separatorY, pageWidth - 1, separatorY, 3, true);
    const int pathMaxWidth = pageWidth - metrics.contentSidePadding * 2;
    // Left-truncate so the deepest directory is always visible
    const char* pathStr = basepath.c_str();
    const char* pathDisplay = pathStr;
    char leftTruncBuf[256];
    if (renderer.getTextWidth(SMALL_FONT_ID, pathStr) > pathMaxWidth) {
      const char ellipsis[] = "\xe2\x80\xa6";  // UTF-8 ellipsis (…)
      const int ellipsisWidth = renderer.getTextWidth(SMALL_FONT_ID, ellipsis);
      const int available = pathMaxWidth - ellipsisWidth;
      // Walk forward from the start until the suffix fits, skipping UTF-8 continuation bytes
      const char* p = pathStr;
      while (*p) {
        if (renderer.getTextWidth(SMALL_FONT_ID, p) <= available) break;
        ++p;
        while (*p && (static_cast<unsigned char>(*p) & 0xC0) == 0x80) ++p;
      }
      snprintf(leftTruncBuf, sizeof(leftTruncBuf), "%s%s", ellipsis, p);
      pathDisplay = leftTruncBuf;
    }
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, pathY, pathDisplay);
  }

  // Help text
  const bool emptyList = rowCount == 0;
  const char* backLabel = (basepath == "/") ? (mode == Mode::PickFirmware ? tr(STR_BACK) : tr(STR_HOME)) : tr(STR_BACK);
  // Confirm-hint label follows the highlighted row: Search / Clear on the synthetic
  // rows; STR_SELECT for a .bin in the firmware picker (Confirm returns the path);
  // STR_OPEN otherwise (open book / descend folder / open recents).
  const char* confirmLabel = "";
  if (!emptyList) {
    const RowRef r = rowAt(selectorIndex);
    if (r.kind == RowKind::Search) {
      confirmLabel = tr(STR_SEARCH);
    } else if (r.kind == RowKind::Clear) {
      confirmLabel = tr(STR_CLEAR_BUTTON);
    } else if (r.kind == RowKind::File && mode == Mode::PickFirmware) {
      const std::string name = fileNameAt(r.fileIndex);
      confirmLabel = !name.empty() && name.back() != '/' ? tr(STR_SELECT) : tr(STR_OPEN);
    } else {
      confirmLabel = tr(STR_OPEN);
    }
  }
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, emptyList ? "" : tr(STR_DIR_UP),
                                            emptyList ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Everything outside the list that can change while the list geometry stays
  // identical goes into the frame identity: the folder (header + path line),
  // the active search (row content), and the selection-driven Confirm hint.
  // Any change there forces a full-panel refresh instead of a windowed one.
  uint32_t frameHash = list_window::hash32(basepath.c_str());
  frameHash = list_window::hash32(activeSearchQuery.c_str(), frameHash);
  frameHash = list_window::hash32(confirmLabel, frameHash);
  list_window::present(renderer, pendingFullRefresh ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH, frameHash);
  pendingFullRefresh = false;
}

size_t FileBrowserActivity::findEntry(const std::string& name) const {
  // Returned value is a selector row index, so offset past the synthetic header
  // rows (Recent Books shortcut + Search row). Callers use this only after a
  // navigation, where the search is cleared (no Clear row).
  const size_t offset = static_cast<size_t>(headerRowCount());
  if (sdMode) {
    size_t slot = sdIndex.lowerBound(name);
    if (slot < sdIndex.count() && sdIndex.nameAt(slot) == name) return slot + offset;
    if (slot >= sdIndex.count() && sdIndex.count() > 0) slot = sdIndex.count() - 1;
    return slot + offset;
  }
  for (size_t i = 0; i < files.size(); i++)
    if (files[i] == name) return i + offset;
  // Not found (e.g. the file was just moved/deleted): land on the slot it used to
  // occupy so whatever shifted up into its place — the "next" file — is selected.
  // Relies on the list being sorted (sortFileList); harmless approximation otherwise.
  size_t slot = 0;
  while (slot < files.size() && FsHelpers::naturalFileLess(files[slot], name)) slot++;
  // A name sorting after every remaining entry lands one PAST the end — clamp to
  // the last real row, or the selector would index files[files.size()] on Confirm.
  if (slot >= files.size() && !files.empty()) slot = files.size() - 1;
  return slot + offset;
}
