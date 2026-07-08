#include "FileBrowserActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <esp_random.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "LibrarySearchSupport.h"
#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"

namespace {
constexpr unsigned long GO_HOME_MS = 1000;
constexpr size_t NAME_BUFFER_SIZE = 500;
}  // namespace

// Defined below; forward-declared so openSearch()'s preview lambda can use it.
std::string getFileName(std::string filename);

void FileBrowserActivity::loadFiles() {
  files.clear();

  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    return;
  }

  root.rewindDirectory();

  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "fileNameBuffer not allocated");
    root.close();
    return;
  }

  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
    if ((!SETTINGS.showHiddenFiles && fileNameBuffer[0] == '.') ||
        strcmp(fileNameBuffer.get(), "System Volume Information") == 0) {
      continue;
    }

    if (file.isDirectory()) {
      files.emplace_back(std::string(fileNameBuffer.get()) + "/");
    } else {
      std::string_view filename{fileNameBuffer.get()};
      if (mode == Mode::PickFirmware) {
        // Firmware picker: only show .bin files.
        if (FsHelpers::checkFileExtension(filename, ".bin")) {
          files.emplace_back(filename);
        }
      } else {
        // Wallpaper files are shown filtered by the chosen format, so /sleep and
        // /sleep pause list exactly the files the sleep rotation will use (only
        // .pxc in PXC mode, only .bmp in BMP mode) instead of always .bmp.
        const bool wallpaper = SETTINGS.wallpaperFormat == CrossPointSettings::WALLPAPER_PXC
                                   ? FsHelpers::checkFileExtension(filename, ".pxc")
                                   : FsHelpers::hasBmpExtension(filename);
        if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
            FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename) || wallpaper) {
          files.emplace_back(filename);
        }
      }
    }
  }
  root.close();
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

  // Gate the "Search current folder" row on the folder actually holding a book
  // (Books mode only). Images/dirs alone don't offer search.
  folderHasBooks_ = false;
  if (mode == Mode::Books) {
    for (const auto& f : files) {
      if (!f.empty() && f.back() == '/') continue;
      const std::string_view sv{f};
      if (FsHelpers::hasEpubExtension(sv) || FsHelpers::hasXtcExtension(sv) || FsHelpers::hasTxtExtension(sv) ||
          FsHelpers::hasMarkdownExtension(sv)) {
        folderHasBooks_ = true;
        break;
      }
    }
  }
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
  filteredIndexes = LibrarySearchSupport::rankMatches(files, activeSearchQuery);
}

void FileBrowserActivity::clearSearch() {
  activeSearchQuery.clear();
  filteredIndexes.clear();
}

void FileBrowserActivity::openSearch() {
  auto keyboard = std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH), activeSearchQuery,
                                                          /*maxLength=*/64, InputType::Text);
  // Live preview: re-rank `files` on every keystroke and show the top matches on the
  // keyboard screen (same ranker as the applied filter, so preview == result).
  keyboard->setLivePreview([this](const std::string& query, int maxRows) -> KeyboardEntryActivity::KbPreviewResult {
    KeyboardEntryActivity::KbPreviewResult out;
    if (query.empty()) return out;
    const auto ranked = LibrarySearchSupport::rankMatches(files, query);
    out.total = static_cast<int>(ranked.size());
    for (int i = 0; i < static_cast<int>(ranked.size()) && i < maxRows; i++) {
      out.rows.push_back(getFileName(files[ranked[i]]));
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

  requestUpdate();
}

void FileBrowserActivity::onExit() {
  Activity::onExit();
  files.clear();
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
    const std::string& entry = files[fileIndex];
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

            requestUpdate(true);
          } else {
            LOG_ERR("FileBrowser", "Failed to delete: %s", fullPath.c_str());
          }
        } else {
          LOG_DBG("FileBrowser", "Delete cancelled by user");
        }
      };

      std::string heading = tr(STR_DELETE) + std::string("? ");

      startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, entry), handler);
      return;
    } else {
      // --- SHORT PRESS ACTION: OPEN/NAVIGATE ---
      if (basepath.back() != '/') basepath += "/";

      if (isDirectory) {
        basepath += entry.substr(0, entry.length() - 1);
        clearSearch();  // search is scoped to the folder you're standing in
        loadFiles();
        selectorIndex = 0;
        requestUpdate();
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
  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
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

std::string getFileExtension(std::string filename) {
  if (filename.back() == '/') {
    return "";
  }
  const auto pos = filename.rfind('.');
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
              return getFileName(files[r.fileIndex]);
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
              return UITheme::getFileIcon(files[r.fileIndex]);
          }
          return None;
        },
        [this](int index) -> std::string {
          const RowRef r = rowAt(index);
          return r.kind == RowKind::File ? getFileExtension(files[r.fileIndex]) : std::string("");
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
    } else if (r.kind == RowKind::File && mode == Mode::PickFirmware && files[r.fileIndex].back() != '/') {
      confirmLabel = tr(STR_SELECT);
    } else {
      confirmLabel = tr(STR_OPEN);
    }
  }
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, emptyList ? "" : tr(STR_DIR_UP),
                                            emptyList ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

size_t FileBrowserActivity::findEntry(const std::string& name) const {
  // Returned value is a selector row index, so offset past the synthetic header
  // rows (Recent Books shortcut + Search row). Callers use this only after a
  // navigation, where the search is cleared (no Clear row).
  const size_t offset = static_cast<size_t>(headerRowCount());
  for (size_t i = 0; i < files.size(); i++)
    if (files[i] == name) return i + offset;
  // Not found (e.g. the file was just moved/deleted): land on the slot it used to
  // occupy so whatever shifted up into its place — the "next" file — is selected.
  // Relies on the list being sorted (sortFileList); harmless approximation otherwise.
  size_t slot = 0;
  while (slot < files.size() && FsHelpers::naturalFileLess(files[slot], name)) slot++;
  return slot + offset;
}
