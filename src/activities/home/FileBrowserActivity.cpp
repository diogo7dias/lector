#include "FileBrowserActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Utf8.h>
#include <esp_random.h>

#include <algorithm>
#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/boot_sleep/PxcSleepRenderer.h"
#include "activities/home/LibrarySearch.h"
#include "activities/network/NearbyFileTransferActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/BusyBanner.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"
#include "util/BookFiling.h"
#include "util/BookFilingNames.h"
#include "util/BookProgressFile.h"
#include "util/BrowserRowFile.h"
#include "util/BusyTick.h"
#include "util/DeferredFavorite.h"
#include "util/FavoriteImageNames.h"
#include "util/TaskWatchdog.h"

namespace fui = freeink::ui;

namespace {
constexpr unsigned long GO_HOME_MS = 1000;
constexpr size_t NAME_BUFFER_SIZE = 500;
// Cache sentinel: this entry's badge has not been looked up yet. A real answer is -1
// (never opened) or 0-100, so it cannot collide with one.
constexpr int16_t PERCENT_NOT_LOOKED_UP = -2;
}  // namespace

namespace {

// A file's FAT modification stamp packed into one comparable number: the date in the high
// half and the time in the low half, which is exactly the order FAT stores them in, so a
// plain integer compare is a chronological compare. Zero when the file carries no stamp,
// which sorts it to the bottom of a newest-first listing.
uint32_t modifiedStamp(HalFile& file) {
  uint16_t fdate = 0, ftime = 0;
  if (!file.getModifyDateTime(&fdate, &ftime)) return 0;
  return (static_cast<uint32_t>(fdate) << 16) | ftime;
}

}  // namespace

// Defined below, next to the other list-label helpers.
std::string getFileName(std::string filename);
std::string getFileExtension(const std::string& filename);

void FileBrowserActivity::loadFiles() {
  // Drop the old search mapping even when opening the new folder fails.
  searchQuery.clear();
  filtered.clear();
  folderHasEntries = false;

  // Armed here rather than at each of the seven call sites, so every path into a
  // folder gets the same treatment. Nothing is drawn unless the scan actually
  // drags — a small folder still lists instantly with no extra panel refresh.
  BusyBanner banner(renderer, tr(STR_BUSY_READING_FOLDER));

  files.clear();
  readingPercents.clear();
  sortKeys.clear();

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

  // Gathering stops the moment a key cannot be stored: applyBrowserOrder() then leaves the
  // listing alphabetical rather than sorting it against a half-built key table.
  bool gatherKeys = needsSortKeys();

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
      // Folders take the top key, so a date order leaves them where the alphabetical sort
      // put them instead of scattering them through the books.
      if (gatherKeys && !pushSortKey(UINT32_MAX)) gatherKeys = false;
    } else {
      std::string_view filename{fileNameBuffer.get()};
      if (mode == Mode::PickFolder) {
        // Folders only: the picker exists to name a destination, and a file in the
        // list would only be a row that cannot be chosen.
      } else if (mode == Mode::PickFirmware) {
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
        // Recently Added reads the stamp off the handle the scan already holds — reopening
        // the file for it would double the SD work of listing a folder. Last Read cannot:
        // its key lives in the book's cache directory, so it is gathered in a second pass
        // once the listing is known.
        if (gatherKeys) {
          // A wallpaper folder is keyed on favorite/not and nothing else: the two groups
          // sort alphabetically inside themselves, which is what the folder looked like
          // before any of them were starred.
          uint32_t key = 0;
          if (isWallpaperFolder()) {
            key = FavoriteImage::hasFavoriteSuffix(filename) ? 0 : 1;
          } else if (SETTINGS.bookBrowserOrder == CrossPointSettings::BOOK_ORDER_RECENTLY_ADDED) {
            key = modifiedStamp(file);
          }
          if (!pushSortKey(key)) gatherKeys = false;
        }
      }
    }
  }
  root.close();
  if (files.truncated()) {
    LOG_ERR("FileBrowser", "Folder too large to list in full; showing first %u entries",
            static_cast<unsigned>(files.size()));
  }
  if (!gatherKeys || sortKeys.size() != files.size()) sortKeys.clear();
  applyBrowserOrder();

  folderHasEntries = !files.empty();

  prewarmRowGlyphs();
}

// One SD pass for every CJK filename in the folder; repaints then hit the
// resident tables instead of re-reading per-string. Adapted from upstream
// #3071, which batches in rebuildRowItems() -- this browser has no row-item
// cache, so the batch runs once per listing and the getter re-derives each
// label through rowTitle().
void FileBrowserActivity::prewarmRowGlyphs() const {
  renderer.prewarmFallbackText(
      UI_10_FONT_ID,
      [](const void* ctx, uint32_t i) -> const char* {
        const auto* self = static_cast<const FileBrowserActivity*>(ctx);
        self->prewarmScratch = self->rowTitle(static_cast<int>(i));
        return self->prewarmScratch.c_str();
      },
      this, static_cast<uint32_t>(totalRowCount()));
  // The bottom path band is not part of the row batch, so it gets its own pass.
  renderer.prewarmFallbackText(UI_10_FONT_ID, basepath.c_str());
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
  if (index < 0) return std::string();
  const std::string rawName(files[static_cast<size_t>(index)]);
  // A favorite queued from the image viewer has not been renamed on the card yet, so the
  // listing still holds the old name. Draw the name the queue is going to give it, or the
  // row reads as not favorited and the press looks lost. Empty when nothing is queued,
  // which is the normal case and costs one scan of a short queue.
  //
  // The queue is keyed by the path on the card, so the lookup runs on the RAW name and the
  // display formatting runs afterwards. Keying it on the label instead (getFileName has
  // already dropped the extension and prefixed "[F] ") matched no queued job at all, and
  // favoriting from the viewer came back to a row that looked untouched.
  return getFileName(browser_row::rowFile(basepath, rawName, DeferredFavorite::pendingTargetFor));
}

int FileBrowserActivity::readingPercentAt(const int index) const {
  if (index < 0 || static_cast<size_t>(index) >= files.size()) return -1;

  if (readingPercents.size() != files.size()) {
    // std::vector's growth throws, and a throwing allocation aborts this build. Probe the
    // size through the nothrow path first: a folder too big to memoise draws no badge
    // rather than taking the device down mid-render.
    auto probe = makeUniqueNoThrow<int16_t[]>(files.size());
    if (!probe) return -1;
    probe.reset();
    readingPercents.assign(files.size(), PERCENT_NOT_LOOKED_UP);
  }

  int16_t& cached = readingPercents[static_cast<size_t>(index)];
  if (cached != PERCENT_NOT_LOOKED_UP) return cached;

  cached = -1;
  const std::string_view name = files[static_cast<size_t>(index)];
  if (!name.empty() && name.back() == '/') return cached;  // a folder reads nothing
  // Comics carry no progress badge anywhere in the firmware (the home list leaves them out
  // too), so they are rejected here rather than costing an SD open that finds nothing.
  if (FsHelpers::hasXtcExtension(name)) return cached;

  book_progress::Marker marker;
  if (book_progress::readForBook(browser_row::cardPath(basepath, name), marker)) cached = marker.percent;
  return cached;
}

std::string FileBrowserActivity::rowValue(const int row) const {
  if (rowKindAt(row) != RowKind::Entry) return {};
  const int index = fileIndexAt(row);
  if (index < 0) return {};

  // File type only; the reading progress is the row's italic line (rowBadge).
  return getFileExtension(std::string(files[static_cast<size_t>(index)]));
}

std::string FileBrowserActivity::rowBadge(const int row) const {
  if (rowKindAt(row) != RowKind::Entry) return {};
  const int percent = readingPercentAt(fileIndexAt(row));
  if (percent < 0) return {};
  if (percent >= 100) return tr(STR_READ_BADGE);
  return std::to_string(percent) + "%";
}

void FileBrowserActivity::applySearch(const std::string& query) {
  {
    // The render task walks `filtered` to map rows onto files; swapping it mid-draw
    // would index the wrong entries.
    RenderLock lock(*this);
    searchQuery = query;
    filtered = searchActive() ? librarysearch::rankMatches(files, searchQuery) : std::vector<int>{};
    // Land on the first result, which is the whole point of having searched.
    moveSelectionTo(std::min(headerRowCount(), std::max(0, totalRowCount() - 1)));
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

// The two folders the sleep screen draws from. Listing them is triage — you are deciding
// what to keep — so the ones already kept sink to the bottom and the undecided pile is
// what you land on.
bool FileBrowserActivity::isWallpaperFolder() const {
  std::string folder = basepath;
  while (folder.size() > 1 && folder.back() == '/') folder.pop_back();
  return folder == "/sleep" || folder == "/sleep pause";
}

bool FileBrowserActivity::needsSortKeys() const {
  if (mode != Mode::Books) return false;  // the firmware picker wants a predictable list
  if (isWallpaperFolder()) return true;
  return SETTINGS.bookBrowserOrder == CrossPointSettings::BOOK_ORDER_RECENTLY_ADDED ||
         SETTINGS.bookBrowserOrder == CrossPointSettings::BOOK_ORDER_LAST_READ;
}

bool FileBrowserActivity::pushSortKey(const uint32_t key) {
  if (sortKeys.size() == sortKeys.capacity()) {
    // std::vector's growth throws, and a throwing allocation aborts this build. Probe the
    // next capacity through the nothrow path first, the way NameList does for its own
    // offset table, so a folder too big to key drops back to alphabetical instead.
    const size_t next = sortKeys.capacity() ? sortKeys.capacity() * 2 : 64;
    auto probe = makeUniqueNoThrow<uint32_t[]>(next);
    if (!probe) {
      LOG_ERR("FileBrowser", "No room to sort %u entries by date; keeping alphabetical order",
              static_cast<unsigned>(sortKeys.size()));
      return false;
    }
    probe.reset();
    sortKeys.reserve(next);
  }
  sortKeys.push_back(key);
  return true;
}

void FileBrowserActivity::fillLastReadKeys() {
  for (size_t i = 0; i < files.size(); i++) {
    if (sortKeys[i] == UINT32_MAX) continue;  // a folder, already keyed to the top

    // One SD open per book, so a folder of hundreds of them is a long blocking stretch on
    // the loop task: let the busy banner appear and keep the watchdog fed.
    if ((i & 0x07) == 0) {
      busy::tick();
      resetTaskWatchdogIfSubscribed();
    }

    book_progress::Marker marker;
    const std::string name(files[i]);
    sortKeys[i] = book_progress::readForBook(browser_row::cardPath(basepath, name), marker) ? marker.readOrder : 0;
  }
}

void FileBrowserActivity::applyBrowserOrder() {
  // A date order sorts by its keys directly, with the name comparator breaking ties, so
  // folders (keyed to the top) stay alphabetical above the books and books read in the same
  // session stay alphabetical among themselves. Every other order starts alphabetical.
  const bool dateOrder = needsSortKeys() && sortKeys.size() == files.size();
  if (dateOrder) {
    if (!isWallpaperFolder() && SETTINGS.bookBrowserOrder == CrossPointSettings::BOOK_ORDER_LAST_READ) {
      busy::tickNow();  // always slow, so the banner is worth showing up front
      fillLastReadKeys();
    }
    if (files.sortByKeyDesc(sortKeys.data(),
                            [](const char* a, const char* b) { return FsHelpers::fileListLessC(a, b); })) {
      sortKeys.clear();
      return;
    }
    LOG_ERR("FileBrowser", "No room to reorder %u entries; keeping alphabetical order",
            static_cast<unsigned>(files.size()));
  }
  sortKeys.clear();

  files.sortByC([](const char* a, const char* b) { return FsHelpers::fileListLessC(a, b); });

  // Books only, and only for Random: the firmware picker keeps its predictable list, and
  // shuffling folders would make navigating a deep card a lottery.
  if (mode != Mode::Books || isWallpaperFolder() ||
      SETTINGS.bookBrowserOrder != CrossPointSettings::BOOK_ORDER_RANDOM) {
    return;
  }

  size_t first = 0;
  while (first < files.size() && !files[first].empty() && files[first].back() == '/') first++;
  if (files.size() - first < 2) return;
  // Fisher-Yates over the file tail, moving offsets rather than names. esp_random() is
  // the hardware RNG, so this needs no seeding and gives a different order every time
  // the folder is opened.
  files.shuffleTail(first, [] { return static_cast<uint32_t>(esp_random()); });
}

void FileBrowserActivity::onEnter() {
  UiListActivity::onEnter();
  RenderLock lock(*this);

  fileNameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "malloc failed for name buffer");
    return;
  }

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
    moveSelectionTo(findEntryRow(fileName));
  } else {
    loadFiles();
  }
}

void FileBrowserActivity::onExit() {
  UiListActivity::onExit();
  // Deliberately NO DeferredFavorite::flush() here. This runs when the browser opens an
  // image viewer too, which would rename the file the viewer is about to show and put the
  // card work back on exactly the press this defers it off. The queue drains when a book
  // is opened or closed, at home, and at the lock. See DeferredFavorite.h.
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

void FileBrowserActivity::confirmDelete(const std::string& fullPath) {
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
          // The row that took the deleted one's place, or the new last row.
          moveSelectionTo(std::max(0, std::min(nav.selected, totalRowCount() - 1)));
        }

        requestUpdate(true);
      } else {
        LOG_ERR("FileBrowser", "Failed to delete: %s", fullPath.c_str());
        GUI.drawPopup(renderer, tr(STR_DELETE_FAILED));
        delay(1200);
        requestUpdate(true);
      }
    } else {
      LOG_DBG("FileBrowser", "Delete cancelled by user");
    }
  };

  const std::string heading = tr(STR_DELETE) + std::string("? ");
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading,
                                             utf8ComposeNfc(std::string(bookfiling::fileNameOf(fullPath)))),
      handler);
}

void FileBrowserActivity::activateRow(const int row, const bool holdAction) {
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
  const bool isDirectory = (!entry.empty() && entry.back() == '/');
  std::string cleanBasePath = basepath;
  if (cleanBasePath.back() != '/') cleanBasePath += "/";

  // Firmware picker: select file -> return path; navigate into directories normally.
  if (mode == Mode::PickFirmware && !isDirectory) {
    ActivityResult res{FilePathResult{cleanBasePath + entry}};
    res.isCancelled = false;
    setResult(std::move(res));
    finish();
    return;
  }

  if (mode == Mode::Books && holdAction) {
    const std::string fullPath = cleanBasePath + entry;
    // A folder is not sent anywhere and is not moved from here, so its hold goes
    // straight to the delete confirmation rather than opening a pop-up.
    if (isDirectory) {
      confirmDelete(fullPath);
      return;
    }
    const bool sendable = nearby_file::isAcceptedFilename(entry);
    StrId actions[3];
    int count = 0;
    if (sendable) actions[count++] = StrId::STR_NEARBY_SEND_FILE;
    actions[count++] = StrId::STR_MOVE_TO_FOLDER;
    actions[count++] = StrId::STR_DELETE;
    fileActionPopup.show(StrId::STR_FILE_ACTIONS, actions, count, 0, [this, fullPath, sendable](const int choice) {
      const int adjusted = sendable ? choice : choice + 1;
      if (adjusted == 0) {
        activityManager.replaceActivity(std::make_unique<NearbyFileTransferActivity>(
            renderer, mappedInput, NearbyFileTransferActivity::Mode::Send, fullPath));
      } else if (adjusted == 1) {
        promptMoveToFolder(fullPath);
      } else {
        confirmDelete(fullPath);
      }
    });
    requestUpdate();
    return;
  }

  if (!isDirectory) {
    onSelectBook(cleanBasePath + entry);
    return;
  }
  {
    // Same race as the delete path: swap the listing under the render lock.
    RenderLock lock(*this);
    basepath = cleanBasePath + entry.substr(0, entry.length() - 1);
    loadFiles();
  }
  moveSelectionTo(0);
}

bool FileBrowserActivity::handleCustomInput() {
  // While the file-action pop-up is up it owns every button, so nothing below
  // can move the selection or open a file underneath it.
  if (fileActionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  // The folder picker answers with the folder it is standing in, on the button whose
  // label says so.
  if (mode == Mode::PickFolder && mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    ActivityResult res{FilePathResult{basepath}};
    res.isCancelled = false;
    setResult(std::move(res));
    finish();
    return true;
  }
  return false;
}

bool FileBrowserActivity::handleButtons() {
  // Confirm carries two actions, so it cannot fire on the press: the firmware has to
  // wait to learn which one was meant. The hold half fires the moment the threshold
  // passes, and the delete it opens asks for confirmation anyway.
  switch (confirmHold.update(mappedInput.wasPressed(MappedInputManager::Button::Confirm),
                             mappedInput.isPressed(MappedInputManager::Button::Confirm),
                             mappedInput.wasReleased(MappedInputManager::Button::Confirm), mappedInput.getHeldTime(),
                             GO_HOME_MS)) {
    case hold_button::Fired::Hold:
      if (nav.selected < totalRowCount()) activateRow(nav.selected, /*holdAction=*/true);
      return true;
    case hold_button::Fired::Short:
      if (nav.selected < totalRowCount()) activateRow(nav.selected, /*holdAction=*/false);
      return true;
    case hold_button::Fired::None:
      break;
  }

  // Back holds to jump to the root folder and taps to go up one directory, but only in
  // Books mode below the root. Anywhere else it carries no hold action, so it fires on
  // the press like any single-action button.
  const bool backHasHold = mode == Mode::Books && basepath != "/";
  const auto backFired = backHasHold
                             ? backHold.update(mappedInput.wasPressed(MappedInputManager::Button::Back),
                                               mappedInput.isPressed(MappedInputManager::Button::Back),
                                               mappedInput.wasReleased(MappedInputManager::Button::Back),
                                               mappedInput.getHeldTime(), GO_HOME_MS)
                             : backHold.updatePressOnly(mappedInput.wasPressed(MappedInputManager::Button::Back));
  if (backFired == hold_button::Fired::Hold) {
    {
      RenderLock lock(*this);
      basepath = "/";
      loadFiles();
    }
    moveSelectionTo(0);
    return true;
  }
  if (backFired != hold_button::Fired::Short) return false;

  if (basepath != "/") {
    const std::string oldPath = basepath;
    int row = 0;
    {
      RenderLock lock(*this);
      basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
      if (basepath.empty()) basepath = "/";
      loadFiles();
      row = static_cast<int>(findEntryRow(oldPath.substr(oldPath.find_last_of('/') + 1) + "/"));
    }
    moveSelectionTo(row);
  } else if (mode == Mode::PickFirmware || mode == Mode::PickFolder) {
    // A picker at root: cancel back to the caller instead of going home.
    ActivityResult res;
    res.isCancelled = true;
    setResult(std::move(res));
    finish();
  } else {
    onGoHome();
  }
  return true;
}

std::string getFileName(std::string filename) {
  // Display copy only — `files[]` keeps the raw directory-entry bytes, because
  // FAT long-filename lookup is byte-exact: an NFC-normalized path would fail
  // to open the NFD entry macOS wrote. Composing here fixes rendering (fonts
  // carry precomposed syllables / letters only) without touching paths.
  filename = utf8ComposeNfc(filename);
  if (filename.back() == '/') {
    filename.pop_back();
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
  return pos == std::string::npos ? std::string() : filename.substr(pos + 1);
}

void FileBrowserActivity::buildScreen(UiScreen& screen) {
  if (totalRowCount() == 0) {
    screen.centeredText(mode == Mode::PickFirmware ? tr(STR_NO_BIN_FILES) : tr(STR_NO_FILES_FOUND));
    return;
  }
  static const char* const MORE = "\xE2\x80\xBA";
  const int count = totalRowCount();
  rows.assign(count, fui::ListItem{});
  // Three strings per row (title, progress, file type) the items point into.
  rowText.assign(static_cast<size_t>(count) * 3, std::string());
  for (int row = 0; row < count; ++row) {
    auto& item = rows[row];
    std::string* text = &rowText[static_cast<size_t>(row) * 3];
    item.actionValue = static_cast<int16_t>(row);
    text[0] = rowTitle(row);
    item.label = text[0].c_str();
    const int index = fileIndexAt(row);
    const std::string_view name = index >= 0 ? files[static_cast<size_t>(index)] : std::string_view();
    if (index < 0 || (!name.empty() && name.back() == '/')) {
      item.value = MORE;  // the search rows and folders open something
      continue;
    }
    // Progress under the title, in the italic line; the file type as the value.
    text[1] = rowBadge(row);
    if (!text[1].empty()) item.subtitle = text[1].c_str();
    text[2] = rowValue(row);
    item.value = text[2].c_str();
  }

  fui::ListProps props{};
  props.items = rows.data();
  props.count = static_cast<uint16_t>(count);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  // A long name wraps rather than being cut, so the whole title stays readable.
  props.labelText.maxLines = 3;
  screen.list(props);
}

void FileBrowserActivity::activateIndex(const int index) { activateRow(index, /*holdAction=*/false); }

ListChrome FileBrowserActivity::chrome() const {
  ListChrome chrome;
  chrome.title = mode == Mode::PickFirmware ? tr(STR_SELECT_FIRMWARE_FILE)
                 : mode == Mode::PickFolder ? tr(STR_MOVE_TO_FOLDER)
                 : basepath == "/"          ? tr(STR_SD_CARD)
                                            : basepath.c_str() + basepath.rfind('/') + 1;
  // A folder too big to list in full must say so; a silently short listing would read
  // as missing files. A search that matched nothing says that instead of an empty list.
  if (files.truncated()) {
    chrome.note = tr(STR_PARTIAL_LISTING);
  } else if (searchActive() && filtered.empty()) {
    chrome.note = tr(STR_NO_FILES_FOUND);
  }
  // Where in the card the listing comes from, at the foot. The root needs no path.
  if (basepath != "/") chrome.footnotes[0] = basepath.c_str();

  // Only the Books browser goes Home from the root; both pickers cancel back to the caller.
  chrome.backHint = (basepath == "/" && mode == Mode::Books) ? tr(STR_HOME) : tr(STR_BACK);
  const bool listEmpty = totalRowCount() == 0;
  const int selectedFile = fileIndexAt(nav.selected);
  // In PickFirmware mode, Confirm on a .bin returns the path to the caller; folders still open.
  const bool selectingFirmwareFile = mode == Mode::PickFirmware && selectedFile >= 0 &&
                                     !files[static_cast<size_t>(selectedFile)].empty() &&
                                     files[static_cast<size_t>(selectedFile)].back() != '/';
  chrome.confirmHint = listEmpty                                                              ? ""
                       : (rowKindAt(nav.selected) != RowKind::Entry || selectingFirmwareFile) ? tr(STR_SELECT)
                                                                                              : tr(STR_OPEN);
  if (mode == Mode::PickFolder) {
    // Move Here lives on a key the rows cannot show, so its hint stays on every board.
    chrome.thirdHint = tr(STR_MOVE_HERE);
    chrome.contentsKeepsHints = true;
  }
  return chrome;
}

// The browser is commonly reached straight from screens that paint only in FAST (the OPDS
// download fires 20+ full-screen FAST paints of its own), so the panel arrives here already
// carrying ghosts. One FULL on the first frame after entry or resume; later frames stay FAST.
HalDisplay::RefreshMode FileBrowserActivity::refreshMode() {
  if (!pendingFullRefresh) return HalDisplay::FAST_REFRESH;
  pendingFullRefresh = false;
  return HalDisplay::FULL_REFRESH;
}

bool FileBrowserActivity::drawOverlay() { return fileActionPopup.processRender(renderer, mappedInput); }

void FileBrowserActivity::promptMoveToFolder(const std::string& fullPath) {
  startActivityForResult(std::make_unique<FileBrowserActivity>(renderer, mappedInput, "/", Mode::PickFolder),
                         [this, fullPath](const ActivityResult& result) { onMoveDestinationResult(fullPath, result); });
}

void FileBrowserActivity::onMoveDestinationResult(const std::string& fullPath, const ActivityResult& result) {
  pendingFullRefresh = true;
  if (result.isCancelled) {
    requestUpdate();
    return;
  }
  const auto* destination = std::get_if<FilePathResult>(&result.data);
  if (!destination) {
    requestUpdate();
    return;
  }

  std::string folder = destination->path;
  if (folder.empty()) folder = "/";
  // Strip the trailing slash: the filing helper composes the path itself, and the
  // root is spelt as the empty folder there.
  while (folder.size() > 1 && folder.back() == '/') folder.pop_back();
  const std::string folderArg = folder == "/" ? std::string(bookfiling::ROOT_FOLDER) : folder;

  const std::string sourceFolder = fullPath.substr(0, fullPath.find_last_of('/'));
  if ((sourceFolder.empty() ? "/" : sourceFolder) == folder) {
    // Already in that folder: a rename onto itself is not a move.
    requestUpdate();
    return;
  }

  {
    // Same race as the delete path: the listing changes under the render task.
    RenderLock lock(*this);
    // One helper does the whole move: the file, its cache directory, its recents
    // entry and the resume pointer, which are all keyed by the old path.
    const std::string target = bookfiling::buildFolderDestination(fullPath, folderArg.c_str());
    bookfiling::moveBookToFolder(fullPath, target);
    loadFiles();
    if (nav.selected >= totalRowCount()) moveSelectionTo(0);
  }
  requestUpdate();
}

size_t FileBrowserActivity::findEntryRow(const std::string& name) const {
  // Returns a LIST ROW, not an index into `files`: the search rows sit above the
  // entries, so the two only coincide in a folder that has no rows above.
  for (size_t i = 0; i < files.size(); i++)
    if (files[i] == std::string_view(name)) return static_cast<size_t>(headerRowCount()) + i;
  return 0;
}
