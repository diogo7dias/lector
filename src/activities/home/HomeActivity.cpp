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

#include <algorithm>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/skull12.h"
#include "fontIds.h"

int HomeActivity::menuRowCount() const {
  int count = 3;  // File Browser, File transfer, Settings
  if (hasOpdsServers) {
    count++;
  }
  return count;
}

int HomeActivity::getMenuItemCount() const { return static_cast<int>(recentBooks.size()) + menuRowCount(); }

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
    const int menuIndex = selectorIndex - static_cast<int>(recentBooks.size());
    switch (indexToMenuItem(menuIndex, hasOpdsServers)) {
      case HomeMenuItem::FILE_BROWSER:
        onFileBrowserOpen();
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
  drawHomeHeaderExtras();

  // In-progress books as a list: each book's full title + its author wrapped over
  // as many lines as it needs, with an inline [NN%] black-background badge, and
  // "N more above/below" indicators when it scrolls. Replaces the single cover tile —
  // no per-book cover generation, so the home stays fast. A menu selection passes -1
  // so no book row is highlighted.
  // Build menu items dynamically
  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_FILE_TRANSFER), tr(STR_SETTINGS_TITLE)};
  std::vector<UIIcon> menuIcons = {Folder, Transfer, Settings};

  if (hasOpdsServers) {
    menuItems.insert(menuItems.begin() + 1, tr(STR_OPDS_BROWSER));
    menuIcons.insert(menuIcons.begin() + 1, Library);
  }

  if (metrics.homeContinueReadingInMenu && !recentBooks.empty()) {
    // Insert Continue Reading at the top if enabled in theme
    menuItems.insert(menuItems.begin(), tr(STR_CONTINUE_READING));
    menuIcons.insert(menuIcons.begin(), Book);
  }

  // drawButtonMenu lays its rows out from the top of this rect and ignores the height,
  // so any row the home screen does not draw leaves its gap at the BOTTOM — which is
  // what dropping Recent Books produced: a hole between Settings and the button hints.
  // Bottom-anchor the block instead, so the gap under the last row equals the gap
  // between rows. std::max keeps the original top as a floor, so a long book list can
  // still push the menu down but never up into itself.
  const int menuCount = static_cast<int>(menuItems.size());
  const int menuTopFloor = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
  const int lastRowBottomWanted = pageHeight - metrics.buttonHintsHeight - metrics.menuSpacing;
  const int menuBlockHeight =
      metrics.verticalSpacing + (menuCount - 1) * (metrics.menuRowHeight + metrics.menuSpacing) + metrics.menuRowHeight;
  const int menuTop = std::max(menuTopFloor, lastRowBottomWanted - menuBlockHeight);

  // The in-progress list gets every row between the header and the menu, rather than a
  // fixed tile height: with the menu bottom-anchored, whatever the menu does not use is
  // reading material. homeCoverTileHeight stays the floor so a theme that wants a short
  // list still gets one.
  const Rect bookRect{
      0, metrics.homeTopPadding, pageWidth,
      std::max(metrics.homeCoverTileHeight, menuTop - metrics.homeTopPadding - metrics.verticalSpacing)};
  const int bookSelected = (selectorIndex < static_cast<int>(recentBooks.size())) ? selectorIndex : -1;
  const ListVisibility vis = GUI.drawRecentBookList(renderer, bookRect, recentBooks, bookSelected, scrollOffset);
  firstVisibleBookIdx = vis.firstVisible;
  lastVisibleBookIdx = vis.lastVisible;
  scrollOffset = vis.firstVisible;

  GUI.drawButtonMenu(
      renderer,
      Rect{0, menuTop, pageWidth,
           pageHeight - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing +
                         metrics.homeMenuTopOffset + metrics.buttonHintsHeight)},
      menuCount, metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size(),
      [&menuItems](int index) { return std::string(menuItems[index]); },
      [&menuIcons](int index) { return menuIcons[index]; });

  // Back's hint must match what it actually does. An empty label draws no
  // button box at all, which is what HOME_BACK_NONE wants.
  const char* backLabel = "";
  switch (SETTINGS.homeBackAction) {
    case CrossPointSettings::HOME_BACK_RESUME:
      backLabel = recentBooks.empty() ? "" : tr(STR_RESUME);
      break;
    case CrossPointSettings::HOME_BACK_NONE:
    default:
      break;
  }

  const auto labels = mappedInput.mapLabels(backLabel, tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void HomeActivity::drawHomeHeaderExtras() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  // topPadding already carries the X4's physical top-edge crop, so anchoring to it
  // keeps both of these on screen on either board without a per-site inset.
  const int textY = metrics.topPadding + 5;

  // Firmware version at the left edge, which is where the old Lector home carried it.
  const int versionX = metrics.contentSidePadding;
  const int versionWidth = renderer.getTextWidth(UI_10_FONT_ID, CROSSPOINT_VERSION);
  renderer.drawText(UI_10_FONT_ID, versionX, textY, CROSSPOINT_VERSION);

  // Clock to the left of the battery cluster. Only boards with an RTC report
  // available, so this simply does not draw where there is no clock to read.
  // Placed against the same cluster width drawHeader reserves, so the gap stays put
  // whatever the UI font measures.
  int rightEdge = pageWidth - BaseTheme::batteryClusterWidth(renderer) - 12;
  char timeBuf[9];
  if (halClock.isAvailable() &&
      halClock.formatTime(timeBuf, sizeof(timeBuf), SETTINGS.clockUtcOffsetQ, SETTINGS.clockFormat == 1)) {
    const int clockWidth = renderer.getTextWidth(UI_10_FONT_ID, timeBuf);
    rightEdge -= clockWidth;
    renderer.drawText(UI_10_FONT_ID, rightEdge, textY, timeBuf);
  }

  // Skull centred in whatever is left between the version and the right-hand
  // cluster, so it stays put as the version string or the clock changes width.
  // Drawn only when the gap can hold it with air on both sides.
  const int gapLeft = versionX + versionWidth;
  constexpr int skullMinAir = 8;
  if (rightEdge - gapLeft >= Skull12Icon.w + skullMinAir * 2) {
    const int skullX = gapLeft + (rightEdge - gapLeft - Skull12Icon.w) / 2;
    // Sit the skull's centre of mass on the text's own vertical middle, so it lines
    // up with the version string rather than with the invisible line box.
    const int textCenterY = textY + renderer.getTextHeight(UI_10_FONT_ID) / 2;
    const int skullY = textCenterY - Skull12Icon.opticalCenterY;
    renderer.drawIcon(Skull12Icon.bits, skullX, skullY, Skull12Icon.w);
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }
