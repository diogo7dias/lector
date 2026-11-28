#include "LibraryActivity.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibraryIndexFile.h>
#include <Logging.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "activities/util/BookInfoHelper.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/BusyBanner.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/BookBadgeLabel.h"
#include "util/BookCacheUtils.h"
#include "util/BookFilingNames.h"
#include "util/BookProgressFile.h"
#include "util/BusyTick.h"
#include "util/LibraryViewLabels.h"
#include "util/TaskWatchdog.h"

namespace fui = freeink::ui;

namespace {
bool builtThisBoot = false;
uint32_t builtAtGeneration = 0;

constexpr unsigned long HOLD_ACTIONS_MS = 700;

void feedWatchdog(void*) {
  resetTaskWatchdogIfSubscribed();
  busy::tick();
}

bool readBookProgress(const char* path, uint8_t& percentOut, uint32_t& readOrderOut) {
  if (!path || FsHelpers::hasXtcExtension(std::string_view(path))) return false;
  book_progress::Marker marker;
  if (!book_progress::readForBook(path, marker)) return false;
  percentOut = marker.percent;
  readOrderOut = marker.readOrder;
  return true;
}
}  // namespace

library::LibrarySort LibraryActivity::activeSort() const {
  return SETTINGS.librarySort < static_cast<uint8_t>(library::LibrarySort::Count)
             ? static_cast<library::LibrarySort>(SETTINGS.librarySort)
             : library::LibrarySort::Title;
}

library::LibraryFilter LibraryActivity::activeFilter() const {
  return SETTINGS.libraryFilter < static_cast<uint8_t>(library::LibraryFilter::Count)
             ? static_cast<library::LibraryFilter>(SETTINGS.libraryFilter)
             : library::LibraryFilter::All;
}

void LibraryActivity::onEnter() {
  // One lock across the base lifecycle and the build: the base schedules a paint, and the
  // render task must not read the index while the walk replaces it.
  RenderLock lock(*this);
  UiListActivity::onEnter();
  refreshIndex(false);
}

void LibraryActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
  viewOrder.clear();
  viewActive = false;
}

void LibraryActivity::rebuildView() {
  const auto sort = activeSort();
  const auto filter = activeFilter();
  viewOrder.clear();
  viewActive = false;
  if (bookCount > 0 && (sort != library::LibrarySort::Title || filter != library::LibraryFilter::All)) {
    library::LibraryIndexFile index;
    if (index.open(library::libraryIndexPath()) &&
        library::buildLibraryView(index, sort, filter, viewOrder, feedWatchdog, nullptr)) {
      viewActive = true;
    }
  }
  recount();
}

// Rebuild when the index is missing, was built before something changed on the card, or
// `force`. Takes the render lock itself when the caller does not hold it.
void LibraryActivity::refreshIndex(const bool force) {
  const uint32_t generation = Storage.bookGeneration();
  const bool useMetadata = SETTINGS.libraryUseMetadata != 0;
  library::LibraryIndexFile index;
  bool usable = !force && builtThisBoot && builtAtGeneration == generation && index.open(library::libraryIndexPath()) &&
                index.metadataEnabled() == useMetadata;
  if (!usable) {
    index.close();
    BusyBanner banner(renderer, tr(STR_LIBRARY_REBUILDING));
    library::BuildStats stats;
    const uint32_t startMs = millis();
    LOG_INF("LIB", "rebuilding, free heap %u", static_cast<unsigned>(ESP.getFreeHeap()));
    if (library::buildLibraryIndex("/", library::libraryIndexPath(), stats, feedWatchdog, nullptr, useMetadata,
                                   readBookProgress)) {
      builtThisBoot = true;
      builtAtGeneration = generation;
    }
    LOG_INF("LIB", "rebuilt in %u ms, min free heap %u", static_cast<unsigned>(millis() - startMs),
            static_cast<unsigned>(ESP.getMinFreeHeap()));
    usable = index.open(library::libraryIndexPath());
  }
  bookCount = usable ? index.bookCount() : 0;
  failed = !usable;
  index.close();
  // A new index renumbers the books, so the hits of a search no longer name them.
  searchQuery.clear();
  hits.clear();
  totalHits = 0;
  rebuildView();
  nav.reset();
  if (startRow > 0 && startRow < rowCount) {
    nav.selected = startRow;
    nav.followOnBuild = true;
  }
  startRow = 0;
  requestUpdate();
}

bool LibraryActivity::lightPanelAuxText(char* out, const size_t length) const {
  snprintf(out, length, "%s: %s", tr(STR_SORT), I18N.get(library_view::sortLabel(activeSort())));
  return true;
}

bool LibraryActivity::lightPanelStepAux(const int delta) {
  constexpr int count = static_cast<int>(library::LibrarySort::Count);
  const int next = (static_cast<int>(SETTINGS.librarySort) + delta % count + count) % count;
  if (next == SETTINGS.librarySort) return false;
  SETTINGS.librarySort = static_cast<uint8_t>(next);
  SETTINGS.saveToFile();
  {
    RenderLock lock(*this);
    rebuildView();
    nav.reset();
  }
  requestUpdate();
  return true;
}

bool LibraryActivity::lightPanelAux2Text(char* out, const size_t length) const {
  snprintf(out, length, "%s: %s", tr(STR_FILTER), I18N.get(library_view::filterLabel(activeFilter())));
  return true;
}

bool LibraryActivity::lightPanelStepAux2(const int delta) {
  constexpr int count = static_cast<int>(library::LibraryFilter::Count);
  const int next = (static_cast<int>(SETTINGS.libraryFilter) + delta % count + count) % count;
  if (next == SETTINGS.libraryFilter) return false;
  SETTINGS.libraryFilter = static_cast<uint8_t>(next);
  SETTINGS.saveToFile();
  if (searchActive()) {
    applySearch(searchQuery);
  } else {
    {
      RenderLock lock(*this);
      rebuildView();
      nav.reset();
    }
    requestUpdate();
  }
  return true;
}

void LibraryActivity::openSearch() {
  app.clearTapFlash();
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH_ALL_BOOKS),
                                                                 searchQuery, 64, InputType::Text),
                         [this](const ActivityResult& result) {
                           const auto* typed = std::get_if<KeyboardResult>(&result.data);
                           if (result.isCancelled || !typed) {
                             requestUpdate();
                             return;
                           }
                           applySearch(typed->text);
                         });
}

// One pass over the index, title and author of every matching book, keeping the best MAX_HITS.
// The records and the two strings borrowed are the window's own: the render task only
// reads them under the same lock, and rebuilds them on its next paint.
void LibraryActivity::applySearch(const std::string& query) {
  {
    RenderLock lock(*this);
    searchQuery = query;
    hits.clear();
    totalHits = 0;
    const auto filter = activeFilter();
    if (searchActive()) {
      BusyBanner banner(renderer, tr(STR_LIBRARY_SEARCHING));
      library::LibraryIndexFile index;
      if (index.open(library::libraryIndexPath())) {
        for (int first = 0; first < bookCount; first += WINDOW) {
          feedWatchdog(nullptr);
          const size_t wanted = static_cast<size_t>(std::min(WINDOW, bookCount - first));
          const size_t got = index.readRecords(static_cast<uint16_t>(first), wanted, records);
          for (size_t i = 0; i < got; i++) {
            if (!library::matchesFilter(records[i], filter)) continue;
            if (!index.readTitle(records[i], titles[0]) || !index.readAuthor(records[i], authors[0])) continue;
            librarysearch::Match byTitle;
            librarysearch::Match byAuthor;
            const bool title = librarysearch::scoreEntry(titles[0], searchQuery, byTitle);
            const bool author = librarysearch::scoreEntry(authors[0], searchQuery, byAuthor);
            if (!title && !author) continue;
            // The better of the two, the title on a tie.
            const bool authorWins = author && (!title || byAuthor.tier < byTitle.tier ||
                                               (byAuthor.tier == byTitle.tier && byAuthor.score < byTitle.score));
            hits.offer(first + static_cast<int>(i), authorWins ? byAuthor : byTitle);
            totalHits++;
          }
          if (got < wanted) break;
        }
      }
      LOG_INF("LIB", "search \"%s\": %d match, %u kept", searchQuery.c_str(), totalHits,
              static_cast<unsigned>(hits.size()));
    } else {
      rebuildView();
    }
    recount();
    // Land on the first result, which is the point of having searched.
    nav.reset(searchActive() && hits.size() > 0 ? headerRows() : 0);
  }
  requestUpdate();
}

int LibraryActivity::loadEntries(const int first, const int wanted) {
  library::LibraryIndexFile index;
  int got = 0;
  if (index.open(library::libraryIndexPath())) {
    if (searchActive() || viewActive) {
      while (got < wanted && index.readRecord(static_cast<uint16_t>(ordinalAt(first + got)), records[got])) got++;
    } else {
      got = static_cast<int>(index.readRecords(static_cast<uint16_t>(first), static_cast<size_t>(wanted), records));
    }
    for (int i = 0; i < got; i++) {
      if (!index.readTitle(records[i], titles[i]) || !index.readAuthor(records[i], authors[i])) {
        got = i;
        break;
      }
    }
  }
  for (int i = 0; i < got; i++) {
    LOG_DBG("LIB", "row %d: %s | %s", first + i, titles[i].c_str(), authors[i].c_str());
  }
  return got;
}

void LibraryActivity::buildScreen(UiScreen& screen) {
  if (bookCount == 0) {
    screen.centeredText(failed ? tr(STR_LIBRARY_REBUILD_FAILED) : tr(STR_LIBRARY_EMPTY));
    return;
  }

  fui::ListProps props{};
  props.count = static_cast<uint16_t>(rowCount);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  // Viewport first: the window has to cover the rows the sync just decided on.
  syncListViewport(screen, props, /*hasSubtitle=*/true);

  const int header = headerRows();
  const int start = std::max(0, std::min(static_cast<int>(props.topIndex), rowCount));
  const int wanted = std::max(0, std::min({nav.visibleRows + 1, WINDOW, rowCount - start}));
  // The window can open on the search rows; the books follow them in the slots.
  const int headerInWindow = std::max(0, std::min(header - start, wanted));
  const int firstEntry = std::max(0, start - header);
  const int entriesWanted = wanted - headerInWindow;
  const int got = loadEntries(firstEntry, entriesWanted);
  if (got < entriesWanted) {
    // The index changed or failed under us: list what could be read rather than rows
    // the list would then ask for and find missing.
    LOG_ERR("LIB", "read %d of %d rows at %d", got, entriesWanted, firstEntry);
    rowCount = header + firstEntry + got;
  }
  // loadEntries fills from slot 0; move the books behind the search rows.
  for (int i = got - 1; i >= 0 && headerInWindow > 0; i--) {
    titles[i + headerInWindow] = std::move(titles[i]);
    authors[i + headerInWindow] = std::move(authors[i]);
    records[i + headerInWindow] = records[i];
  }
  const int shown = headerInWindow + got;
  rows.assign(static_cast<size_t>(shown), fui::ListItem{});

  const bool portrait = renderer.getOrientation() == GfxRenderer::Orientation::Portrait;
  for (int i = 0; i < shown; ++i) {
    const int row = start + i;
    rows[i].actionValue = static_cast<int16_t>(row);
    badges[i][0] = '\0';
    if (row == 0) {
      if (searchActive()) {
        searchLabel = std::string(tr(STR_EDIT_SEARCH)) + " " + searchQuery;
        snprintf(hitsLine, sizeof(hitsLine), tr(STR_LIBRARY_HITS), totalHits);
        rows[i].label = searchLabel.c_str();
        rows[i].subtitle = hitsLine;
      } else {
        rows[i].label = tr(STR_SEARCH_ALL_BOOKS);
      }
      continue;
    }
    if (row < header) {
      rows[i].label = tr(STR_CLEAR_SEARCH);
      continue;
    }
    rows[i].label = titles[i].c_str();
    if (!authors[i].empty()) rows[i].subtitle = authors[i].c_str();
    const int pct = library::isRecordUnread(records[i]) ? -1 : static_cast<int>(records[i].percent);
    const std::string chip = book_badge::chipLabel(pct, tr(STR_READ_BADGE));
    if (!chip.empty()) {
      snprintf(badges[i], sizeof(badges[i]), "%s", chip.c_str());
      rows[i].value = badges[i];
    }
    // Icons only in portrait, as in the other lists: the assets cannot be rotated.
    if (portrait) rows[i].icon = listIconFor(UIIcon::Book, 32);
  }

  // One SD pass for the page's fallback glyphs (CJK titles), as the chapter list does.
  struct PrewarmCtx {
    const std::string* titles;
    const std::string* authors;
    size_t rows;
  } ctx{titles + headerInWindow, authors + headerInWindow, static_cast<size_t>(got)};
  renderer.prewarmFallbackText(
      UI_10_FONT_ID,
      [](const void* c, const uint32_t i) -> const char* {
        const auto* p = static_cast<const PrewarmCtx*>(c);
        if (i >= p->rows * 2) return nullptr;
        return (i % 2 == 0 ? p->titles[i / 2] : p->authors[i / 2]).c_str();
      },
      &ctx, static_cast<uint32_t>(got * 2));

  props.count = static_cast<uint16_t>(rowCount);  // loadEntries may have shortened it
  props.items = rows.data();
  props.itemsWindowFirst = static_cast<uint16_t>(start);
  props.itemsWindowCount = static_cast<uint16_t>(rows.size());
  screen.list(props);
}

void LibraryActivity::activateIndex(const int index) {
  if (index < 0 || index >= rowCount) return;
  if (index < headerRows()) {
    if (index == 0) {
      openSearch();
    } else {
      applySearch(std::string());
    }
    return;
  }
  std::string path;
  {
    // The render task reads the index too; keep it off the card while this handle seeks.
    RenderLock lock(*this);
    library::LibraryIndexFile file;
    library::ClixRecord record;
    if (file.open(library::libraryIndexPath()) &&
        file.readRecord(static_cast<uint16_t>(ordinalAt(index - headerRows())), record)) {
      file.readPath(record, path);
    }
    file.close();
    // A row can only name a missing file if something changed the card behind the
    // generation count (the card edited on a computer). Rebuild rather than open nothing.
    if (path.empty() || !Storage.exists(path.c_str())) {
      LOG_INF("LIB", "row %d names a missing book, rebuilding", index);
      refreshIndex(/*force=*/true);
      return;
    }
  }
  LOG_INF("LIB", "open %s", path.c_str());
  // Remember the row position so Back from the book lands on it.
  APP_STATE.libraryRow = static_cast<uint16_t>(searchActive() ? ordinalAt(index - headerRows()) + 1 : index);
  app.clearTapFlash();
  activityManager.goToReader(path);
}

void LibraryActivity::onRowLongPress(const int index) {
  if (index < headerRows() || index >= rowCount) return;
  std::string path;
  library::ClixRecord record{};
  {
    RenderLock lock(*this);
    library::LibraryIndexFile file;
    if (file.open(library::libraryIndexPath()) &&
        file.readRecord(static_cast<uint16_t>(ordinalAt(index - headerRows())), record)) {
      file.readPath(record, path);
    }
    file.close();
    if (path.empty() || !Storage.exists(path.c_str())) {
      refreshIndex(/*force=*/true);
      return;
    }
  }

  StrId actions[3];
  int count = 0;
  if (FsHelpers::hasEpubExtension(path)) actions[count++] = StrId::STR_BOOK_INFO;
  if (FsHelpers::hasEpubExtension(path) || FsHelpers::hasTextBookExtension(path)) {
    actions[count++] = library::isRecordRead(record) ? StrId::STR_MARK_UNREAD : StrId::STR_MARK_READ;
  }
  actions[count++] = StrId::STR_DELETE;

  app.clearTapFlash();
  bookActionPopup.show(StrId::STR_FILE_ACTIONS, actions, count, 0, [this, path, actions](const int choice) {
    switch (actions[choice]) {
      case StrId::STR_BOOK_INFO:
        openBookInfoScreen(*this, renderer, mappedInput, path, [this] { requestUpdate(); });
        break;
      case StrId::STR_MARK_READ:
      case StrId::STR_MARK_UNREAD: {
        const bool ok = actions[choice] == StrId::STR_MARK_READ ? markBookRead(path) : markBookUnread(path);
        if (ok) {
          RenderLock lock(*this);
          rebuildView();
          if (nav.selected >= rowCount && rowCount > 0) nav.selected = rowCount - 1;
        }
        requestUpdate();
        break;
      }
      default:
        confirmDelete(path);
        break;
    }
  });
  requestUpdate();
}

void LibraryActivity::confirmDelete(const std::string& fullPath) {
  const std::string heading = tr(STR_DELETE) + std::string("? ");
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading,
                                             utf8ComposeNfc(std::string(bookfiling::fileNameOf(fullPath)))),
      [this, fullPath](const ActivityResult& res) {
        if (!res.isCancelled) {
          if (deleteOpenedBook(fullPath)) {
            RenderLock lock(*this);
            refreshIndex(false);
          } else {
            GUI.drawPopup(renderer, tr(STR_DELETE_FAILED));
            delay(1200);
            requestUpdate(true);
          }
        } else {
          requestUpdate();
        }
      });
}

bool LibraryActivity::handleCustomInput() {
  return bookActionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

bool LibraryActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onBackButton();
    return true;
  }
  switch (confirmHold.update(mappedInput.wasPressed(MappedInputManager::Button::Confirm),
                             mappedInput.isPressed(MappedInputManager::Button::Confirm),
                             mappedInput.wasReleased(MappedInputManager::Button::Confirm), mappedInput.getHeldTime(),
                             HOLD_ACTIONS_MS)) {
    case hold_button::Fired::Hold:
      confirmHold.reset();
      if (nav.selected >= headerRows() && nav.selected < rowCount) onRowLongPress(nav.selected);
      return true;
    case hold_button::Fired::Short:
      if (nav.selected >= 0 && nav.selected < rowCount) activateIndex(nav.selected);
      return true;
    case hold_button::Fired::None:
      break;
  }
  return false;
}

// Back leaves a search before it leaves the Library.
void LibraryActivity::onBackButton() {
  if (searchActive()) {
    applySearch(std::string());
    return;
  }
  finish();
}

ListChrome LibraryActivity::chrome() const {
  ListChrome chrome;
  chrome.title = tr(STR_LIBRARY);
  if (library_view::formatSubHeader(activeSort(), activeFilter(), subHeaderText, sizeof(subHeaderText))) {
    chrome.subHeader = subHeaderText;
  }
  if (bookCount == 0) {
    chrome.confirmHint = "";
    return chrome;
  }
  // Which book is selected; on the search rows, how many there are.
  const int entry = nav.selected - headerRows();
  const int entries = entryCount();
  if (entry >= 0 && entry < entries) {
    snprintf(counter, sizeof(counter), tr(STR_LIBRARY_POSITION), entry + 1, entries);
  } else {
    snprintf(counter, sizeof(counter), tr(STR_LIBRARY_COUNT), entries);
  }
  chrome.headerRight = counter;
  return chrome;
}

bool LibraryActivity::drawOverlay() { return bookActionPopup.processRender(renderer, mappedInput); }
