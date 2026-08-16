#include "FileBrowserActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <esp_random.h>

#include <algorithm>
#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/boot_sleep/PxcSleepRenderer.h"
#include "activities/home/LibrarySearch.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/BusyBanner.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"
#include "util/BusyTick.h"
#include "util/FavoriteImageNames.h"

namespace {
constexpr unsigned long GO_HOME_MS = 1000;
constexpr size_t NAME_BUFFER_SIZE = 500;
}  // namespace

// Defined below, next to the other list-label helpers.
std::string getFileName(std::string filename);
std::string getFileExtension(const std::string& filename);

void FileBrowserActivity::loadFiles() {
  // Armed here rather than at each of the seven call sites, so every path into a
  // folder gets the same treatment. Nothing is drawn unless the scan actually
  // drags — a small folder still lists instantly with no extra panel refresh.
  BusyBanner banner(renderer, tr(STR_BUSY_READING_FOLDER));

  files.clear();
  // A new listing invalidates the scroll window. render() re-derives it from the selection,
  // so starting at the top is safe even when the caller then selects a row further down.
  scrollOffset = 0;
  firstVisibleIdx = 0;
  lastVisibleIdx = 0;

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

  uint32_t scanned = 0;
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    // A wallpaper or library folder can hold thousands of entries, and this scan
    // blocks before the browser's first frame. Let the busy banner appear if it
    // drags.
    if ((++scanned & 0x3F) == 0) busy::tick();
    file.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
    if ((!SETTINGS.showHiddenFiles && fileNameBuffer[0] == '.') ||
        strcmp(fileNameBuffer.get(), "System Volume Information") == 0) {
      continue;
    }

    if (file.isDirectory()) {
      std::string dirName(fileNameBuffer.get());
      dirName += '/';
      if (!files.push(dirName)) break;
    } else {
      std::string_view filename{fileNameBuffer.get()};
      if (mode == Mode::PickFirmware) {
        // Firmware picker: only show .bin files.
        if (FsHelpers::checkFileExtension(filename, ".bin")) {
          if (!files.push(filename)) break;
        }
      } else if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
                 FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename) ||
                 FsHelpers::hasBmpExtension(filename) || FsHelpers::hasPngExtension(filename) ||
                 hasPxcExtension(filename)) {
        // .pxc joins the list so a wallpaper folder can be browsed and triaged on
        // the device; selecting one opens PxcViewerActivity. .png joins it so a
        // transparent sleep overlay can be previewed the same way.
        if (!files.push(filename)) break;
      }
    }
  }
  root.close();
  if (files.truncated()) {
    LOG_ERR("FileBrowser", "Folder too large to list in full; showing first %u entries",
            static_cast<unsigned>(files.size()));
  }
  files.sortByC([](const char* a, const char* b) { return FsHelpers::fileListLessC(a, b); });
  shuffleFilesIfRandomOrder();

  // A new folder is a new search scope, so any running search ends here rather than
  // silently filtering a listing the user never searched.
  searchQuery.clear();
  filtered.clear();
  folderHasEntries = !files.empty();
}

int FileBrowserActivity::headerRowCount() const {
  if (mode != Mode::Books || !folderHasEntries) return 0;  // the firmware picker stays a plain list
  return searchActive() ? 2 : 1;
}

int FileBrowserActivity::entryRowCount() const {
  return static_cast<int>(searchActive() ? filtered.size() : files.size());
}

int FileBrowserActivity::totalRowCount() const { return headerRowCount() + entryRowCount(); }

FileBrowserActivity::RowKind FileBrowserActivity::rowKindAt(const int row) const {
  if (row >= headerRowCount()) return RowKind::Entry;
  return row == 0 ? RowKind::Search : RowKind::ClearSearch;
}

int FileBrowserActivity::fileIndexAt(const int row) const {
  const int entryRow = row - headerRowCount();
  if (entryRow < 0 || entryRow >= entryRowCount()) return -1;
  return searchActive() ? filtered[static_cast<size_t>(entryRow)] : entryRow;
}

std::string FileBrowserActivity::rowTitle(const int row) const {
  switch (rowKindAt(row)) {
    case RowKind::Search:
      return searchActive() ? std::string(tr(STR_EDIT_SEARCH)) + " " + searchQuery
                            : std::string(tr(STR_SEARCH_CURRENT_FOLDER));
    case RowKind::ClearSearch:
      return tr(STR_CLEAR_SEARCH);
    case RowKind::Entry:
      break;
  }
  const int index = fileIndexAt(row);
  return index < 0 ? std::string() : getFileName(std::string(files[static_cast<size_t>(index)]));
}

std::string FileBrowserActivity::rowValue(const int row) const {
  if (rowKindAt(row) != RowKind::Entry) return {};
  const int index = fileIndexAt(row);
  return index < 0 ? std::string() : getFileExtension(std::string(files[static_cast<size_t>(index)]));
}

void FileBrowserActivity::applySearch(const std::string& query) {
  {
    // The render task walks `filtered` to map rows onto files; swapping it mid-draw
    // would index the wrong entries.
    RenderLock lock(*this);
    searchQuery = query;
    filtered = searchActive() ? librarysearch::rankMatches(files, searchQuery) : std::vector<int>{};
    // Land on the first result, which is the whole point of having searched.
    selectorIndex = static_cast<size_t>(std::min(headerRowCount(), std::max(0, totalRowCount() - 1)));
    scrollOffset = 0;
  }
  requestUpdate(true);
}

void FileBrowserActivity::clearSearch() { applySearch(std::string()); }

void FileBrowserActivity::openSearchEntry() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, std::string(tr(STR_SEARCH_CURRENT_FOLDER)),
                                              searchQuery, 64, InputType::Text),
      [this](const ActivityResult& res) {
        if (res.isCancelled) {
          requestUpdate(true);
          return;
        }
        const auto* kr = std::get_if<KeyboardResult>(&res.data);
        applySearch(kr ? kr->text : std::string());
      });
}

void FileBrowserActivity::shuffleFilesIfRandomOrder() {
  // Books only. The firmware picker wants a predictable list, and shuffling folders
  // would make navigating a deep card a lottery — sortFileList puts them first, so
  // the shuffle starts after the last one.
  if (!SETTINGS.bookBrowserRandomOrder || mode != Mode::Books) return;

  size_t first = 0;
  while (first < files.size() && !files[first].empty() && files[first].back() == '/') first++;
  if (files.size() - first < 2) return;
  // Fisher-Yates over the file tail, moving offsets rather than names. esp_random() is
  // the hardware RNG, so this needs no seeding and gives a different order every time
  // the folder is opened.
  files.shuffleTail(first, [] { return static_cast<uint32_t>(esp_random()); });
}

void FileBrowserActivity::onEnter() {
  Activity::onEnter();

  fileNameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "malloc failed for name buffer");
    return;
  }

  selectorIndex = 0;
  pendingFullRefresh = true;

  auto root = Storage.open(basepath.c_str());
  if (!root) {
    basepath = "/";
    loadFiles();
  } else if (!root.isDirectory()) {
    const std::string oldPath = basepath;
    basepath = FsHelpers::extractFolderPath(basepath);
    loadFiles();

    const auto pos = oldPath.find_last_of('/');
    const std::string fileName = oldPath.substr(pos + 1);
    selectorIndex = findEntryRow(fileName);
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
  // Rows have variable heights now, so a fixed items-per-page is meaningless. The page jump
  // steps by however many rows the last draw actually fit on screen.
  const int pageItems = std::max(1, lastVisibleIdx - firstVisibleIdx + 1);

  auto activateSelected = [this](const bool holdAction) {
    const int row = static_cast<int>(selectorIndex);
    if (rowKindAt(row) == RowKind::Search) {
      openSearchEntry();
      return;
    }
    if (rowKindAt(row) == RowKind::ClearSearch) {
      clearSearch();
      return;
    }

    const int fileIndex = fileIndexAt(row);
    if (fileIndex < 0) return;
    const std::string entry(files[static_cast<size_t>(fileIndex)]);
    bool isDirectory = (!entry.empty() && entry.back() == '/');

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

    if (mode == Mode::Books && holdAction) {
      // --- LONG PRESS ACTION: DELETE FILE OR DIRECTORY ---
      std::string cleanBasePath = basepath;
      if (cleanBasePath.back() != '/') cleanBasePath += "/";
      const std::string fullPath = cleanBasePath + entry;

      auto handler = [this, fullPath](const ActivityResult& res) {
        // Nothing to swallow here any more: ActivityManager arms the input gate on
        // every screen change, so a button still held when this screen comes back
        // is ignored until it is released.
        if (!res.isCancelled) {
          LOG_DBG("FileBrowser", "Attempting to delete: %s", fullPath.c_str());
          if (removeDirFile(fullPath)) {
            LOG_DBG("FileBrowser", "Deleted successfully");
            {
              // buildScreen() reads files/basepath on the render task; loadFiles() frees and
              // rebuilds those strings, so the swap has to happen under the render lock.
              RenderLock lock(*this);
              loadFiles();
              const int rows = totalRowCount();
              if (rows == 0) {
                selectorIndex = 0;
              } else if (selectorIndex >= static_cast<size_t>(rows)) {
                // Move selection to the new "last" item
                selectorIndex = static_cast<size_t>(rows - 1);
              }
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
        {
          // Same race as the delete path: swap the listing under the render lock.
          RenderLock lock(*this);
          basepath += entry.substr(0, entry.length() - 1);
          loadFiles();
          selectorIndex = 0;
        }
        requestUpdate();
      } else {
        onSelectBook(basepath + entry);
      }
    }
    return;
  };

  // Confirm carries two actions, so it cannot fire on the press: the firmware has to
  // wait to learn which one was meant. The hold half no longer waits for the release
  // though — it fires the moment the threshold passes, and the delete it opens asks
  // for confirmation anyway.
  switch (confirmHold.update(mappedInput.isPressed(MappedInputManager::Button::Confirm),
                             mappedInput.wasReleased(MappedInputManager::Button::Confirm), mappedInput.getHeldTime(),
                             GO_HOME_MS)) {
    case hold_button::Fired::Hold:
      activateSelected(/*holdAction=*/true);
      return;
    case hold_button::Fired::Short:
      activateSelected(/*holdAction=*/false);
      return;
    case hold_button::Fired::None:
      break;
  }

  // Back holds to jump to the root folder and taps to go up one directory, but only in
  // Books mode below the root. Anywhere else it carries no hold action, so it fires on
  // the press like any single-action button.
  const bool backHasHold = mode == Mode::Books && basepath != "/";
  const auto backFired = backHasHold
                             ? backHold.update(mappedInput.isPressed(MappedInputManager::Button::Back),
                                               mappedInput.wasReleased(MappedInputManager::Button::Back),
                                               mappedInput.getHeldTime(), GO_HOME_MS)
                             : backHold.updatePressOnly(mappedInput.wasPressed(MappedInputManager::Button::Back));
  if (backFired == hold_button::Fired::Hold) {
    {
      RenderLock lock(*this);
      basepath = "/";
      loadFiles();
      selectorIndex = 0;
    }
    requestUpdate();
    return;
  }
  if (backFired == hold_button::Fired::Short) {
    {
      if (basepath != "/") {
        const std::string oldPath = basepath;

        {
          RenderLock lock(*this);
          basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
          if (basepath.empty()) basepath = "/";
          loadFiles();

          const auto pos = oldPath.find_last_of('/');
          const std::string dirName = oldPath.substr(pos + 1) + "/";
          selectorIndex = findEntryRow(dirName);
        }

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

  const int listSize = totalRowCount();

  // Keep the selection inside the drawn window. Moving past the bottom nudges the offset by
  // one and lets the draw settle the rest; jumping above the top snaps the offset to the
  // selection (which also covers the wrap from the first row to the last).
  auto followSelection = [this, listSize] {
    const int index = static_cast<int>(selectorIndex);
    if (index > lastVisibleIdx) {
      scrollOffset = std::min(scrollOffset + 1, listSize - 1);
    }
    if (index < firstVisibleIdx) {
      scrollOffset = index;
    }
    scrollOffset = std::clamp(scrollOffset, 0, std::max(0, listSize - 1));
  };

  buttonNavigator.onNextStep([this, listSize, followSelection] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    followSelection();
    requestUpdate();
  });

  buttonNavigator.onPreviousStep([this, listSize, followSelection] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    followSelection();
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, listSize, pageItems, followSelection] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    followSelection();
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize, pageItems, followSelection] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    followSelection();
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
  // Favourite wallpapers show as "[F] name": the _F suffix is the state, but it is
  // bookkeeping, not part of the name the user gave the file.
  if (FavoriteImage::hasFavoriteSuffix(filename)) {
    return "[F] " + filename.substr(0, pos - 2);
  }
  return filename.substr(0, pos);
}

std::string getFileExtension(const std::string& filename) {
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
  // A folder too big to list in full must say so. Showing a silently short listing
  // would read as missing files.
  if (files.truncated()) {
    folderName += " (";
    folderName += tr(STR_PARTIAL_LISTING);
    folderName += ")";
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str());

  const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int pathReserved = pathLineHeight + metrics.verticalSpacing;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved;
  if (totalRowCount() == 0) {
    const char* emptyMsg = (mode == Mode::PickFirmware) ? tr(STR_NO_BIN_FILES) : tr(STR_NO_FILES_FOUND);
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, emptyMsg);
  } else {
    // Wrapping list: a long book title spills onto extra lines instead of being cut off
    // with an ellipsis. Rows therefore vary in height, so the visible range comes back from
    // the draw and feeds the scroll offset.
    const ListVisibility vis = GUI.drawWrappedList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, totalRowCount(), static_cast<int>(selectorIndex),
        scrollOffset, [this](int row) { return rowTitle(row); }, [this](int row) { return rowValue(row); });
    firstVisibleIdx = vis.firstVisible;
    lastVisibleIdx = vis.lastVisible;
    scrollOffset = vis.firstVisible;
    // A search that matched nothing still shows its two rows, so say so rather than
    // leaving the user staring at an empty list wondering if the search ran.
    if (searchActive() && filtered.empty()) {
      renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + contentHeight / 2,
                        tr(STR_NO_FILES_FOUND));
    }
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
  const char* backLabel = (basepath == "/") ? (mode == Mode::PickFirmware ? tr(STR_BACK) : tr(STR_HOME)) : tr(STR_BACK);
  // In PickFirmware mode, Confirm on a .bin returns the path to the caller (not "open"); show
  // STR_SELECT instead. Directories in the same picker still descend, so keep STR_OPEN there.
  const int selectedRow = static_cast<int>(selectorIndex);
  const int selectedFile = fileIndexAt(selectedRow);
  const bool selectingFirmwareFile = mode == Mode::PickFirmware && selectedFile >= 0 &&
                                     !files[static_cast<size_t>(selectedFile)].empty() &&
                                     files[static_cast<size_t>(selectedFile)].back() != '/';
  const bool listEmpty = totalRowCount() == 0;
  const char* confirmLabel =
      listEmpty ? ""
                : (rowKindAt(selectedRow) != RowKind::Entry ? tr(STR_SELECT)
                                                            : (selectingFirmwareFile ? tr(STR_SELECT) : tr(STR_OPEN)));
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, listEmpty ? "" : tr(STR_DIR_UP),
                                            listEmpty ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // The browser is commonly reached straight from screens that paint only in FAST
  // (the OPDS download fires 20+ full-screen FAST paints of its own), so the panel
  // arrives here already carrying ghosts. Spend one FULL on the first frame after
  // entry or resume; every later frame stays FAST.
  if (pendingFullRefresh) {
    pendingFullRefresh = false;
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  } else {
    renderer.displayBuffer();
  }
}

size_t FileBrowserActivity::findEntryRow(const std::string& name) const {
  // Returns a LIST ROW, not an index into `files`: the search rows sit above the
  // entries, so the two only coincide in a folder that has no rows above.
  for (size_t i = 0; i < files.size(); i++)
    if (files[i] == std::string_view(name)) return static_cast<size_t>(headerRowCount()) + i;
  return 0;
}
