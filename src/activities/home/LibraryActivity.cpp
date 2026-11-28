#include "LibraryActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibraryIndexFile.h>
#include <Logging.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/BusyBanner.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/BookProgressFile.h"
#include "util/BusyTick.h"
#include "util/TaskWatchdog.h"

namespace fui = freeink::ui;

namespace {
// What the card looked like when the index was last built this boot. Another boot, or
// any book written, renamed or removed since, means the index may be out of date.
bool builtThisBoot = false;
uint32_t builtAtGeneration = 0;

void feedWatchdog(void*) {
  resetTaskWatchdogIfSubscribed();
  busy::tick();
}
}  // namespace

void LibraryActivity::onEnter() {
  // One lock across the base lifecycle and the build: the base schedules a paint, and the
  // render task must not read the index while the walk replaces it.
  RenderLock lock(*this);
  UiListActivity::onEnter();
  refreshIndex(false);
}

bool LibraryActivity::isGridMode() const { return SETTINGS.libraryViewMode != 0; }

void LibraryActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
  gridRenderer.invalidateCache();
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
    if (library::buildLibraryIndex("/", library::libraryIndexPath(), stats, feedWatchdog, nullptr, useMetadata)) {
      builtThisBoot = true;
      builtAtGeneration = generation;
    }
    LOG_INF("LIB", "rebuilt in %u ms, min free heap %u", static_cast<unsigned>(millis() - startMs),
            static_cast<unsigned>(ESP.getMinFreeHeap()));
    usable = index.open(library::libraryIndexPath());
  }
  bookCount = usable ? index.bookCount() : 0;
  failed = !usable;
  // A new index renumbers the books, so the hits of a search no longer name them.
  searchQuery.clear();
  hits.clear();
  totalHits = 0;
  recount();
  nav.reset();
  if (startRow > 0 && startRow < rowCount) {
    nav.selected = startRow;
    nav.followOnBuild = true;
    gridSelected = std::max(0, static_cast<int>(startRow) - headerRows());
  } else {
    gridSelected = 0;
  }
  gridCleanPaint = true;
  gridRenderer.invalidateCache();
  startRow = 0;
  requestUpdate();
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

// One pass over the index, title and author of every book, keeping the best MAX_HITS.
// The records and the two strings borrowed are the window's own: the render task only
// reads them under the same lock, and rebuilds them on its next paint.
void LibraryActivity::applySearch(const std::string& query) {
  {
    RenderLock lock(*this);
    searchQuery = query;
    hits.clear();
    totalHits = 0;
    if (searchActive()) {
      BusyBanner banner(renderer, tr(STR_LIBRARY_SEARCHING));
      library::LibraryIndexFile index;
      if (index.open(library::libraryIndexPath())) {
        for (int first = 0; first < bookCount; first += WINDOW) {
          feedWatchdog(nullptr);
          const size_t wanted = static_cast<size_t>(std::min(WINDOW, bookCount - first));
          const size_t got = index.readRecords(static_cast<uint16_t>(first), wanted, records);
          for (size_t i = 0; i < got; i++) {
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
    if (searchActive()) {
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
  }
  const int shown = headerInWindow + got;
  rows.assign(static_cast<size_t>(shown), fui::ListItem{});

  const bool portrait = renderer.getOrientation() == GfxRenderer::Orientation::Portrait;
  for (int i = 0; i < shown; ++i) {
    const int row = start + i;
    rows[i].actionValue = static_cast<int16_t>(row);
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

void LibraryActivity::openEntry(const int entryIndex) {
  const int entries = entryCount();
  if (entryIndex < 0 || entryIndex >= entries) return;
  std::string path;
  {
    // The render task reads the index too; keep it off the card while this handle seeks.
    RenderLock lock(*this);
    library::LibraryIndexFile file;
    library::ClixRecord record;
    if (file.open(library::libraryIndexPath()) &&
        file.readRecord(static_cast<uint16_t>(ordinalAt(entryIndex)), record)) {
      file.readPath(record, path);
    }
    file.close();
    // A row can only name a missing file if something changed the card behind the
    // generation count (the card edited on a computer). Rebuild rather than open nothing.
    if (path.empty() || !Storage.exists(path.c_str())) {
      LOG_INF("LIB", "entry %d names a missing book, rebuilding", entryIndex);
      refreshIndex(/*force=*/true);
      return;
    }
  }
  LOG_INF("LIB", "open %s", path.c_str());
  // The unfiltered row (one header row), so Back from the book lands on it with no search.
  APP_STATE.libraryRow = static_cast<uint16_t>(ordinalAt(entryIndex) + 1);
  app.clearTapFlash();
  activityManager.goToReader(path);
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
  openEntry(index - headerRows());
}

void LibraryActivity::renderGrid() {
  const ListChrome ch = chrome();
  const list_chrome::Bands bands = listChromeBands(renderer, ch);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int screenW = renderer.getScreenWidth();

  renderer.clearScreen();
  const Rect headerRect{0, metrics.topPadding, screenW,
                        BaseTheme::headerHeightFor(renderer, screenW, ch.title, ch.headerRight)};
  GUI.drawHeader(renderer, headerRect, ch.title, nullptr, ch.headerRight);

#if defined(BOARD_HAS_PSRAM)
  constexpr bool kHighRam = true;
#else
  constexpr bool kHighRam = false;
#endif
  const library_grid::Box body{metrics.contentSidePadding, bands.contentTop + 4,
                               screenW - 2 * metrics.contentSidePadding,
                               std::max(80, bands.contentBottom - bands.contentTop - 8)};
  gridLayout = library_grid::computeLayout(body, kHighRam);

  const int total = entryCount();
  if (gridSelected >= total) gridSelected = std::max(0, total - 1);
  const int pageStart = library_grid::pageStartFor(gridSelected, gridLayout.perPage);
  const int wanted = std::min(gridLayout.perPage, total - pageStart);

  library_grid::GridBook pageBooks[library_grid::kMaxPageTiles];
  int got = 0;
  library::LibraryIndexFile index;
  if (wanted > 0 && index.open(library::libraryIndexPath())) {
    if (searchActive()) {
      while (got < wanted && index.readRecord(static_cast<uint16_t>(ordinalAt(pageStart + got)), records[got])) got++;
    } else {
      got = static_cast<int>(index.readRecords(static_cast<uint16_t>(pageStart), static_cast<size_t>(wanted), records));
    }
    for (int i = 0; i < got; ++i) {
      if (!index.readTitle(records[i], pageBooks[i].title) || !index.readAuthor(records[i], pageBooks[i].author) ||
          !index.readPath(records[i], pageBooks[i].path)) {
        got = i;
        break;
      }
      book_progress::Marker marker;
      pageBooks[i].percent = book_progress::readForBook(pageBooks[i].path, marker) ? marker.percent : 255;
    }
  }
  gridVisibleCount = got;
  gridRenderer.drawPage(renderer, gridLayout, pageBooks, got, gridSelected - pageStart, pageStart,
                        Storage.bookGeneration());

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(gridCleanPaint ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  gridCleanPaint = false;
}

void LibraryActivity::render(RenderLock&& lock) {
  if (!isGridMode() || bookCount == 0) {
    UiListActivity::render(std::move(lock));
    return;
  }
  renderGrid();
}

bool LibraryActivity::handleCustomInput() {
  if (!isGridMode() || bookCount == 0) return false;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onBackButton();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openEntry(gridSelected);
    return true;
  }

  const int total = entryCount();
  if (total <= 0) return true;

  const auto scroll = mappedInput.wasListScrollSwipe();
  if (scroll != list_swipe::Scroll::None) {
    const int next = library_grid::movePage(gridSelected, total, gridLayout.perPage,
                                            scroll == list_swipe::Scroll::PageDown ? 1 : -1);
    if (next != gridSelected) {
      gridSelected = next;
      gridCleanPaint = true;
      requestUpdate();
    }
    return true;
  }

  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    const int hit = library_grid::tileAt(gridLayout, gridVisibleCount, tx, ty);
    if (hit >= 0) {
      const int pageStart = library_grid::pageStartFor(gridSelected, gridLayout.perPage);
      gridSelected = pageStart + hit;
      openEntry(gridSelected);
    }
    return true;
  }

  auto move = [this, total](const int delta) {
    const int oldPage = library_grid::pageStartFor(gridSelected, gridLayout.perPage);
    const int next = library_grid::moveCursor(gridSelected, total, delta);
    if (next == gridSelected) return;
    gridSelected = next;
    if (library_grid::pageStartFor(gridSelected, gridLayout.perPage) != oldPage) gridCleanPaint = true;
    requestUpdate();
  };
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [&] { move(-gridLayout.cols); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [&] { move(gridLayout.cols); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [&] { move(-1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [&] { move(1); });
  return true;
}

bool LibraryActivity::lightPanelAuxText(char* buf, const size_t size) const {
  snprintf(buf, size, "%s: %s", tr(STR_VIEW), isGridMode() ? tr(STR_VIEW_GRID) : tr(STR_VIEW_LIST));
  return true;
}

bool LibraryActivity::lightPanelStepAux(const int delta) {
  if (delta == 0) return false;
  SETTINGS.libraryViewMode = isGridMode() ? 0 : 1;
  SETTINGS.saveToFile();
  if (isGridMode()) {
    gridSelected = std::max(0, nav.selected - headerRows());
    gridCleanPaint = true;
  } else {
    nav.selected = std::min(rowCount - 1, gridSelected + headerRows());
    nav.followOnBuild = true;
    gridRenderer.invalidateCache();
  }
  requestUpdate();
  return true;
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
  if (bookCount == 0) {
    chrome.confirmHint = "";
    return chrome;
  }
  // Which book is selected; on the search rows, how many there are.
  const int entry = isGridMode() ? gridSelected : (nav.selected - headerRows());
  const int entries = entryCount();
  if (entry >= 0 && entry < entries) {
    snprintf(counter, sizeof(counter), tr(STR_LIBRARY_POSITION), entry + 1, entries);
  } else {
    snprintf(counter, sizeof(counter), tr(STR_LIBRARY_COUNT), entries);
  }
  chrome.headerRight = counter;
  return chrome;
}
