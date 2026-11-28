#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibraryIndexFile.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "LibraryActivity.h"
#include "MappedInputManager.h"
#include "activities/RenderLock.h"
#include "components/UITheme.h"
#include "components/icons/search24.h"
#include "fontIds.h"
#include "util/LibraryViewLabels.h"

namespace {
constexpr unsigned long HOLD_ACTIONS_MS = 700;
}  // namespace

bool LibraryActivity::isGridMode() const { return SETTINGS.libraryViewMode == CrossPointSettings::LIBRARY_VIEW_GRID; }

void LibraryActivity::render(RenderLock&& lock) {
  if (!isGridMode() || bookCount == 0) {
    UiListActivity::render(std::move(lock));
    return;
  }
  renderGrid();
}

void LibraryActivity::renderGrid() {
  const ListChrome ch = chrome();
  const list_chrome::Bands bands = listChromeBands(renderer, ch);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int screenW = renderer.getScreenWidth();

  renderer.clearScreen();
  const int headerH = BaseTheme::headerHeightFor(renderer, screenW, ch.title, ch.headerRight);
  const Rect headerRect{0, metrics.topPadding, screenW, headerH};
  gridHeaderRect = {headerRect.x, headerRect.y, headerRect.width, headerRect.height};
  GUI.drawHeader(renderer, headerRect, ch.title, nullptr, ch.headerRight);
  // Place a search icon beside the header title so touch readers can reach Search in Grid mode.
  const int titleW = renderer.getTextWidth(UI_12_FONT_ID, ch.title, EpdFontFamily::BOLD);
  const int searchX = metrics.contentSidePadding + titleW + 10;
  const int searchY = headerRect.y + (headerRect.height - Search24Icon.h) / 2;
  if (searchX + Search24Icon.w < screenW - 120) {
    renderer.drawIcon(Search24Icon.bits, searchX, searchY, Search24Icon.w);
  }
  if (ch.subHeader && ch.subHeader[0] != '\0') {
    const Rect subRect{0, headerRect.y + headerRect.height, screenW, metrics.tabBarHeight};
    GUI.drawSubHeader(renderer, subRect, ch.subHeader);
  }

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

  int got = 0;
  library::LibraryIndexFile index;
  if (wanted > 0 && index.open(library::libraryIndexPath())) {
    if (searchActive() || viewActive) {
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
      // Read progress straight from the CLX v2 record instead of opening percent.bin per tile.
      pageBooks[i].percent = records[i].percent;
    }
  }
  gridVisibleCount = got;
  gridRenderer.drawPage(renderer, gridLayout, pageBooks, got, gridSelected - pageStart, pageStart,
                        Storage.bookGeneration());

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (bookActionPopup.isActive()) {
    bookActionPopup.render(renderer);
  }
  renderer.displayBuffer(gridCleanPaint ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  gridCleanPaint = false;
}

bool LibraryActivity::handleGridInput() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onBackButton();
    return true;
  }

  const int total = entryCount();
  switch (confirmHold.update(mappedInput.wasPressed(MappedInputManager::Button::Confirm),
                             mappedInput.isPressed(MappedInputManager::Button::Confirm),
                             mappedInput.wasReleased(MappedInputManager::Button::Confirm), mappedInput.getHeldTime(),
                             HOLD_ACTIONS_MS)) {
    case hold_button::Fired::Hold:
      confirmHold.reset();
      if (total > 0 && gridSelected >= 0 && gridSelected < total) onRowLongPress(headerRows() + gridSelected);
      return true;
    case hold_button::Fired::Short:
      if (total > 0 && gridSelected >= 0 && gridSelected < total) openEntry(gridSelected);
      return true;
    case hold_button::Fired::None:
      break;
  }

  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    if (tx >= gridHeaderRect.x && tx < gridHeaderRect.x + gridHeaderRect.width && ty >= gridHeaderRect.y &&
        ty < gridHeaderRect.y + gridHeaderRect.height) {
      openSearch();
      return true;
    }
    if (total > 0) {
      const int hit = library_grid::tileAt(gridLayout, gridVisibleCount, tx, ty);
      if (hit >= 0) {
        const int pageStart = library_grid::pageStartFor(gridSelected, gridLayout.perPage);
        gridSelected = pageStart + hit;
        openEntry(gridSelected);
      }
    }
    return true;
  }

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

bool LibraryActivity::runBoundAction(const uint8_t function) {
  if (function == CrossPointSettings::LP_MENU_SEARCH && bookCount > 0) {
    openSearch();
    return true;
  }
  return false;
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
    gridSelected = 0;
    gridCleanPaint = true;
    gridRenderer.invalidateCache();
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
      gridSelected = 0;
      gridCleanPaint = true;
      gridRenderer.invalidateCache();
    }
    requestUpdate();
  }
  return true;
}

bool LibraryActivity::lightPanelAux3Text(char* out, const size_t length) const {
  snprintf(out, length, "%s: %s", tr(STR_VIEW), isGridMode() ? tr(STR_VIEW_GRID) : tr(STR_VIEW_LIST));
  return true;
}

bool LibraryActivity::lightPanelStepAux3(const int delta) {
  if (delta == 0) return false;
  SETTINGS.libraryViewMode =
      isGridMode() ? CrossPointSettings::LIBRARY_VIEW_LIST : CrossPointSettings::LIBRARY_VIEW_GRID;
  SETTINGS.saveToFile();
  if (isGridMode()) {
    gridSelected = std::max(0, nav.selected - headerRows());
    gridCleanPaint = true;
    gridRenderer.invalidateCache();
  } else {
    nav.selected = std::min(rowCount - 1, gridSelected + headerRows());
    nav.followOnBuild = true;
    gridRenderer.invalidateCache();
  }
  requestUpdate();
  return true;
}
