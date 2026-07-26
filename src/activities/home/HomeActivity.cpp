#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>
#include <Xtc.h>

#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

int HomeActivity::menuRowCount() const {
  int count = 4;  // File Browser, Recents, File transfer, Settings
  if (hasOpdsServers) {
    count++;
  }
  return count;
}

int HomeActivity::pagesTileIndex() const { return static_cast<int>(recentBooks.size()) + menuRowCount(); }

int HomeActivity::getMenuItemCount() const {
  // Books, then the menu rows, then the pages tile.
  //
  // The tile is drawn up in the header but sits LAST in the tab order on purpose.
  // Putting it first (where it looks like it belongs) would make it the selection
  // the home screen opens with, and the first Confirm after waking the device
  // would zero the user's page count instead of opening a book.
  return static_cast<int>(recentBooks.size()) + menuRowCount() + 1;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsServers = OPDS_STORE.hasServers();

  // Load every recent (in-progress) book, up to the store cap; drawList pages them
  // (with up/down arrows) when there are more than fit the list area at once.
  loadRecentBooks(RecentBooksStore::MAX_RECENT_BOOKS);
  scrollOffset = 0;
  firstVisibleBookIdx = 0;
  lastVisibleBookIdx = 0;

  const auto base = static_cast<int>(recentBooks.size());
  selectorIndex = initialMenuItem == HomeMenuItem::NONE ? 0 : base + menuItemToIndex(initialMenuItem, hasOpdsServers);

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() { Activity::onExit(); }

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();

  auto activateSelection = [this] {
    if (selectorIndex < static_cast<int>(recentBooks.size())) {
      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }
    if (selectorIndex == pagesTileIndex()) {
      // Persist straight away: a reset the user can see must survive a power-off
      // before the next state save, or the number comes back.
      APP_STATE.sessionPagesRead = 0;
      APP_STATE.saveToFile();
      requestUpdate();
      return;
    }
    const int menuIndex = selectorIndex - static_cast<int>(recentBooks.size());
    switch (indexToMenuItem(menuIndex, hasOpdsServers)) {
      case HomeMenuItem::FILE_BROWSER:
        onFileBrowserOpen();
        break;
      case HomeMenuItem::RECENTS:
        onRecentsOpen();
        break;
      case HomeMenuItem::OPDS_BROWSER:
        onOpdsBrowserOpen();
        break;
      case HomeMenuItem::FILE_TRANSFER:
        onFileTransferOpen();
        break;
      case HomeMenuItem::SETTINGS_MENU:
        onSettingsOpen();
        break;
      default:
        break;
    }
  };

  const int bookCount = static_cast<int>(recentBooks.size());
  // Keep the selected book within the list's visible window as it moves. drawList
  // clamps and reports the true firstVisible each render, so this only nudges.
  buttonNavigator.onNext([this, menuCount, bookCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    if (selectorIndex < bookCount) {
      if (selectorIndex > lastVisibleBookIdx) scrollOffset++;
      if (selectorIndex < firstVisibleBookIdx) scrollOffset = selectorIndex;
      scrollOffset = std::max(0, std::min(scrollOffset, std::max(0, bookCount - 1)));
    }
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount, bookCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    if (selectorIndex < bookCount) {
      if (selectorIndex < firstVisibleBookIdx) scrollOffset = selectorIndex;
      scrollOffset = std::max(0, std::min(scrollOffset, std::max(0, bookCount - 1)));
    }
    requestUpdate();
  });

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) backPressSeen = true;

  // Back is otherwise unused on the home menu, so it runs the user's configured
  // action. backPressSeen guards against the stale release of the Back press
  // that closed the previous activity.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && backPressSeen) {
    switch (SETTINGS.homeBackAction) {
      case CrossPointSettings::HOME_BACK_RESUME:
        // recentBooks is most-recent-first and already pruned of files missing
        // from the SD card.
        if (!recentBooks.empty()) {
          onSelectBook(recentBooks[0].path);
          return;
        }
        break;
      case CrossPointSettings::HOME_BACK_RECENTS:
        onRecentsOpen();
        return;
      case CrossPointSettings::HOME_BACK_NONE:
      default:
        break;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr);
  drawHomeHeaderExtras(selectorIndex == pagesTileIndex());

  // In-progress books as a list: each book's full title + " by INITIALS" wrapped over
  // as many lines as it needs, with an inline [NN%] black-background badge, and
  // "N more above/below" indicators when it scrolls. Replaces the single cover tile —
  // no per-book cover generation, so the home stays fast. A menu selection passes -1
  // so no book row is highlighted.
  const Rect bookRect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight};
  const int bookSelected = (selectorIndex < static_cast<int>(recentBooks.size())) ? selectorIndex : -1;
  const ListVisibility vis = GUI.drawRecentBookList(renderer, bookRect, recentBooks, bookSelected, scrollOffset);
  firstVisibleBookIdx = vis.firstVisible;
  lastVisibleBookIdx = vis.lastVisible;
  scrollOffset = vis.firstVisible;

  // Build menu items dynamically
  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_MENU_RECENT_BOOKS), tr(STR_FILE_TRANSFER),
                                        tr(STR_SETTINGS_TITLE)};
  std::vector<UIIcon> menuIcons = {Folder, Recent, Transfer, Settings};

  if (hasOpdsServers) {
    menuItems.insert(menuItems.begin() + 2, tr(STR_OPDS_BROWSER));
    menuIcons.insert(menuIcons.begin() + 2, Library);
  }

  if (metrics.homeContinueReadingInMenu && !recentBooks.empty()) {
    // Insert Continue Reading at the top if enabled in theme
    menuItems.insert(menuItems.begin(), tr(STR_CONTINUE_READING));
    menuIcons.insert(menuIcons.begin(), Book);
  }

  GUI.drawButtonMenu(
      renderer,
      Rect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset, pageWidth,
           pageHeight - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing +
                         metrics.homeMenuTopOffset + metrics.buttonHintsHeight)},
      static_cast<int>(menuItems.size()),
      metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size(),
      [&menuItems](int index) { return std::string(menuItems[index]); },
      [&menuIcons](int index) { return menuIcons[index]; });

  // Back's hint must match what it actually does. An empty label draws no
  // button box at all, which is what HOME_BACK_NONE wants.
  const char* backLabel = "";
  switch (SETTINGS.homeBackAction) {
    case CrossPointSettings::HOME_BACK_RESUME:
      backLabel = recentBooks.empty() ? "" : tr(STR_RESUME);
      break;
    case CrossPointSettings::HOME_BACK_RECENTS:
      // Short form: "Recent Books" is wider than the hint box.
      backLabel = tr(STR_RECENTS_HINT);
      break;
    case CrossPointSettings::HOME_BACK_NONE:
    default:
      break;
  }

  const auto labels = mappedInput.mapLabels(backLabel, tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void HomeActivity::drawHomeHeaderExtras(const bool pagesSelected) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  // topPadding already carries the X4's physical top-edge crop, so anchoring to it
  // keeps both of these on screen on either board without a per-site inset.
  const int textY = metrics.topPadding + 5;

  // "Pages" label tile plus an inverted count chip. Filled when selected, since
  // this row sits outside the menu list and gets no selection arrow of its own.
  const std::string label = tr(STR_HOME_PAGES);
  const std::string countText = std::to_string(APP_STATE.sessionPagesRead);
  constexpr int tilePad = 6;
  constexpr int tileGap = 5;
  const int tileH = renderer.getLineHeight(UI_10_FONT_ID) + 6;
  const int tileY = textY - 3;

  // Firmware version at the left edge, which is where the old Lector home carried it.
  // The Pages tile starts after it rather than at the padding.
  const int versionX = metrics.contentSidePadding;
  const int versionWidth = renderer.getTextWidth(UI_10_FONT_ID, CROSSPOINT_VERSION);
  renderer.drawText(UI_10_FONT_ID, versionX, textY, CROSSPOINT_VERSION);

  const int labelTileX = versionX + versionWidth + 14;
  const int labelTextW = renderer.getTextWidth(UI_10_FONT_ID, label.c_str());
  const int labelTileW = labelTextW + tilePad * 2;
  if (pagesSelected) {
    renderer.fillRect(labelTileX, tileY, labelTileW, tileH + 1, true);
  } else {
    renderer.drawRect(labelTileX, tileY, labelTileW, tileH, 2, true);
  }
  renderer.drawText(UI_10_FONT_ID, labelTileX + tilePad, textY, label.c_str(), !pagesSelected);

  const int countTileX = labelTileX + labelTileW + tileGap;
  const int countTextW = renderer.getTextWidth(UI_10_FONT_ID, countText.c_str());
  const int countTileW = countTextW + tilePad * 2;
  renderer.fillRect(countTileX, tileY, countTileW, tileH + 1, true);
  renderer.drawText(UI_10_FONT_ID, countTileX + (countTileW - countTextW) / 2, textY, countText.c_str(), false);

  // Clock to the left of the battery cluster. Only boards with an RTC report
  // available, so this simply does not draw where there is no clock to read.
  if (!halClock.isAvailable()) return;
  char timeBuf[9];
  if (!halClock.formatTime(timeBuf, sizeof(timeBuf), SETTINGS.clockUtcOffsetQ, SETTINGS.clockFormat == 1)) return;
  // Placed against the same cluster width drawHeader reserves, so the gap stays put
  // whatever the UI font measures.
  const int clockWidth = renderer.getTextWidth(UI_10_FONT_ID, timeBuf);
  renderer.drawText(UI_10_FONT_ID, pageWidth - BaseTheme::batteryClusterWidth(renderer) - 12 - clockWidth, textY,
                    timeBuf);
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }
