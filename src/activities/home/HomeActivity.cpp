#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>
#include <Xtc.h>
#include <esp_random.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "LibraryActivity.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "activities/network/NearbyFileTransferActivity.h"
#include "components/BusyBanner.h"
#include "components/UITheme.h"
#include "components/icons/skull12.h"
#include "fontIds.h"
#include "network/opds/BookOrbitMissing.h"
#include "util/BusyTick.h"
#include "util/DeferredFavorite.h"

void HomeActivity::buildMenuRows() {
  menuRowsCount = 0;
  const auto add = [this](const HomeMenuItem item) { menuRows[menuRowsCount++] = item; };
  if (bookOrbitMissing > 0) add(HomeMenuItem::BOOKORBIT_MISSING);
  add(HomeMenuItem::FILE_BROWSER);
  add(HomeMenuItem::LIBRARY);
  if (hasOpdsServers) add(HomeMenuItem::OPDS_BROWSER);
  add(HomeMenuItem::FILE_TRANSFER);
  // Keys only: the Power double click that opens the panel shows nowhere, so it gets a
  // row. A touch board pulls the panel down from the top edge of every screen.
  if (!mappedInput.hasTouch()) add(HomeMenuItem::QUICK_PANEL);
  add(HomeMenuItem::SETTINGS_MENU);
}

int HomeActivity::rowOfMenuItem(const HomeMenuItem item) const {
  for (int i = 0; i < menuRowsCount; i++) {
    if (menuRows[i] == item) return i;
  }
  return 0;
}

int HomeActivity::getMenuItemCount() const { return static_cast<int>(recentBooks.size()) + menuRowsCount; }

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // One SD stat per book, and a full store on a slow card is where a home press
    // spends its seconds.
    busy::tick();

    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }

  // One SD pass for every CJK title and author on the home list; repaints then
  // hit the resident tables instead of re-reading per-string (upstream #3071).
  // Titles and authors draw in the same font and style here (drawRecentBookList
  // joins them into one row string), so a single batch covers both: even
  // indices are titles, odd ones authors.
  renderer.prewarmFallbackText(
      UI_10_FONT_ID,
      [](const void* ctx, uint32_t i) -> const char* {
        const auto& books = *static_cast<const std::vector<RecentBook>*>(ctx);
        const RecentBook& book = books[i / 2];
        return (i % 2 == 0) ? book.title.c_str() : book.author.c_str();
      },
      &recentBooks, static_cast<uint32_t>(recentBooks.size()) * 2);
}

void HomeActivity::onEnter() {
  Activity::onEnter();
  // Reaching home means the user has finished triaging wallpapers, so this is one of the
  // moments queued favorite renames run. A no-op when the queue is empty, which is almost
  // always. See DeferredFavorite.h for why they are not done on the press.
  //
  // Waited on, not fire-and-forget: the recent-books stats below queue behind the
  // worker's directory scans on the storage mutex anyway, so home paints no sooner by
  // letting the worker run alongside. Waiting here instead puts the wait behind a busy
  // strip, which the silent mutex stall never showed.
  DeferredFavorite::flush();
  if (!DeferredFavorite::isIdle()) {
    BusyBanner banner(renderer, tr(STR_CHECKING_WALLPAPERS));
    DeferredFavorite::waitForIdle(15000);
  }
  DeferredFavorite::reconcile();

  hasOpdsServers = OPDS_STORE.hasServers();
  bookOrbitMissing = bookorbit_missing::findServer() ? bookorbit_missing::savedCount() : 0;
  snprintf(bookOrbitMissingLabel, sizeof(bookOrbitMissingLabel), tr(STR_BOOKORBIT_MISSING), bookOrbitMissing);
  buildMenuRows();
  if (SETTINGS.homeBackAction == CrossPointSettings::HOME_BACK_SORTES) {
    BusyBanner banner(renderer, tr(STR_SORTES));
    sortesResult = sortes::findBook(sortesBook, esp_random);
  }

  // Load every recent (in-progress) book, up to the store cap; drawList pages them
  // (with up/down arrows) when there are more than fit the list area at once.
  loadRecentBooks(RecentBooksStore::MAX_RECENT_BOOKS);
  scrollOffset = 0;
  firstVisibleBookIdx = 0;
  lastVisibleBookIdx = 0;

  const auto base = static_cast<int>(recentBooks.size());
  selectorIndex = initialMenuItem == HomeMenuItem::NONE ? 0 : base + rowOfMenuItem(initialMenuItem);

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
    if (menuIndex < 0 || menuIndex >= menuRowsCount) return;
    switch (menuRows[menuIndex]) {
      case HomeMenuItem::BOOKORBIT_MISSING:
        onBookOrbitMissingOpen();
        break;
      case HomeMenuItem::FILE_BROWSER:
        onFileBrowserOpen();
        break;
      case HomeMenuItem::LIBRARY:
        onLibraryOpen();
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
      case HomeMenuItem::QUICK_PANEL:
        // The same entry the Power double click takes.
        activityManager.runBoundAction(CrossPointSettings::LP_MENU_LIGHT_PANEL);
        break;
      default:
        break;
    }
  };

  // A tap opens the row it landed on.
  int tappedItem = 0;
  if (mappedInput.wasRowTapped(tappedItem) && tappedItem >= 0 && tappedItem < menuCount) {
    selectorIndex = tappedItem;
    activateSelection();
    return;
  }

  const int bookCount = static_cast<int>(recentBooks.size());
  // Keep the selected book within the list's visible window as it moves. drawList
  // clamps and reports the true firstVisible each render, so this only nudges.
  const auto moveTo = [this, bookCount](const int index) {
    selectorIndex = index;
    if (selectorIndex < bookCount) {
      if (selectorIndex > lastVisibleBookIdx) scrollOffset += selectorIndex - lastVisibleBookIdx;
      if (selectorIndex < firstVisibleBookIdx) scrollOffset = selectorIndex;
      scrollOffset = std::max(0, std::min(scrollOffset, std::max(0, bookCount - 1)));
    }
    requestUpdate();
  };
  // A tap wraps; a hold stops at the ends, since a hold that wraps never ends.
  buttonNavigator.onNextStep([&] { moveTo(ButtonNavigator::nextIndex(selectorIndex, menuCount)); });
  buttonNavigator.onPreviousStep([&] { moveTo(ButtonNavigator::previousIndex(selectorIndex, menuCount)); });
  // A flick arrives through the continuous step too, as in every list; here it pages
  // the books rather than moving one row.
  buttonNavigator.onNextContinuous([&] {
    if (ButtonNavigator::swipeDrivenPass()) return pageBooks(true);
    moveTo(ButtonNavigator::heldIndex(selectorIndex, menuCount, 1));
  });
  buttonNavigator.onPreviousContinuous([&] {
    if (ButtonNavigator::swipeDrivenPass()) return pageBooks(false);
    moveTo(ButtonNavigator::heldIndex(selectorIndex, menuCount, -1));
  });

  // Back is otherwise unused on the home menu, so it runs the user's configured
  // action. A Back still held from the screen that was left is handled centrally
  // by the input gate ActivityManager arms on every transition.
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    switch (SETTINGS.homeBackAction) {
      case CrossPointSettings::HOME_BACK_RESUME:
        // recentBooks is most-recent-first and already pruned of files missing
        // from the SD card.
        if (!recentBooks.empty()) {
          onSelectBook(recentBooks[0].path);
          return;
        }
        break;
      case CrossPointSettings::HOME_BACK_SORTES:
        // onEnter already drew a random finished book, and nothing on Home changes the
        // card, so a second walk of the whole library would only draw again.
        if (sortesResult == sortes::ScanResult::Found) {
          activityManager.goToReader(sortesBook, false, false, true);
          return;
        }
        break;
      case CrossPointSettings::HOME_BACK_NONE:
      default:
        break;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    activateSelection();
  }
}

void HomeActivity::pageBooks(const bool forward) {
  const int bookCount = static_cast<int>(recentBooks.size());
  // The window the last paint drew; rows wrap, so a page is however many it fitted.
  const int shown = lastVisibleBookIdx - firstVisibleBookIdx + 1;
  const int first = forward ? lastVisibleBookIdx + 1 : std::max(0, firstVisibleBookIdx - shown);
  // Already at that end: nothing moves, so nothing is painted.
  if (forward ? first >= bookCount : firstVisibleBookIdx == 0) return;
  scrollOffset = first;
  selectorIndex = first;
  requestUpdate();
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr);
  // The version, the clock and the skull that sit in the header band; the theme
  // places all three, so a restyle reaches them like everything else.
  char timeBuf[9];
  const bool hasClock =
      halClock.isAvailable() && halClock.formatTime(timeBuf, sizeof(timeBuf), SETTINGS.clockFormat == 1,
                                                    SETTINGS.clockTimezone, SETTINGS.clockDst);
  GUI.drawHomeHeaderExtras(renderer, CROSSPOINT_VERSION, hasClock ? timeBuf : nullptr);

  // In-progress books as a list: each book's full title + its author wrapped over
  // as many lines as it needs, with an inline [NN%] black-background badge, and
  // "N more above/below" indicators when it scrolls. Replaces the single cover tile —
  // no per-book cover generation, so the home stays fast. A menu selection passes -1
  // so no book row is highlighted.
  // The rows buildMenuRows() chose, in its order.
  const char* menuItems[MAX_MENU_ROWS] = {};
  UIIcon menuIcons[MAX_MENU_ROWS] = {};
  for (int i = 0; i < menuRowsCount; i++) {
    switch (menuRows[i]) {
      case HomeMenuItem::BOOKORBIT_MISSING:
        menuItems[i] = bookOrbitMissingLabel;
        menuIcons[i] = Library;
        break;
      case HomeMenuItem::FILE_BROWSER:
        menuItems[i] = tr(STR_BROWSE_FILES);
        menuIcons[i] = Folder;
        break;
      case HomeMenuItem::LIBRARY:
        menuItems[i] = tr(STR_LIBRARY);
        menuIcons[i] = Library;
        break;
      case HomeMenuItem::OPDS_BROWSER:
        menuItems[i] = tr(STR_OPDS_BROWSER);
        menuIcons[i] = Library;
        break;
      case HomeMenuItem::FILE_TRANSFER:
        menuItems[i] = tr(STR_NEARBY_SYNC);
        menuIcons[i] = Transfer;
        break;
      case HomeMenuItem::SETTINGS_MENU:
        menuItems[i] = tr(STR_SETTINGS_TITLE);
        menuIcons[i] = Settings;
        break;
      case HomeMenuItem::QUICK_PANEL:
        menuItems[i] = tr(STR_QUICK_PANEL);
        menuIcons[i] = Settings;
        break;
      default:
        menuItems[i] = "";
        menuIcons[i] = None;
        break;
    }
  }

  // drawButtonMenu lays its rows out from the top of this rect and ignores the height,
  // so any row the home screen does not draw leaves its gap at the BOTTOM — which is
  // what dropping Recent Books produced: a hole between Settings and the button hints.
  // Bottom-anchor the block instead, so the gap under the last row equals the gap
  // between rows. No floor under the top: a sixth row (the BookOrbit one) has to rise
  // into the list's space, or Settings lands on the button hints.
  const int menuCount = menuRowsCount;
  const int lastRowBottomWanted = pageHeight - metrics.buttonHintsHeight - metrics.menuSpacing;
  const int menuBlockHeight =
      metrics.verticalSpacing + (menuCount - 1) * (metrics.menuRowHeight + metrics.menuSpacing) + metrics.menuRowHeight;
  const int menuTop = std::max(metrics.homeTopPadding, lastRowBottomWanted - menuBlockHeight);

  // The in-progress list gets every row between the header and the menu, rather than a
  // fixed tile height: with the menu bottom-anchored, whatever the menu does not use is
  // reading material.
  const Rect bookRect{0, metrics.homeTopPadding, pageWidth,
                      std::max(0, menuTop - metrics.homeTopPadding - metrics.verticalSpacing)};
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
      menuCount, selectorIndex - recentBooks.size(), [&menuItems](int index) { return std::string(menuItems[index]); },
      [&menuIcons](int index) { return menuIcons[index]; },
      // The books above own indices 0..N-1 of the same selection space the menu continues.
      static_cast<int>(recentBooks.size()));

  // Back's hint must match what it actually does. An empty label draws no
  // button box at all, which is what HOME_BACK_NONE wants.
  const char* backLabel = "";
  switch (SETTINGS.homeBackAction) {
    case CrossPointSettings::HOME_BACK_RESUME:
      backLabel = recentBooks.empty() ? "" : tr(STR_RESUME);
      break;
    case CrossPointSettings::HOME_BACK_SORTES:
      backLabel = sortesResult == sortes::ScanResult::Found   ? tr(STR_SORTES)
                  : sortesResult == sortes::ScanResult::Empty ? tr(STR_SORTES_EMPTY)
                                                              : tr(STR_SORTES_UNAVAILABLE);
      break;
    case CrossPointSettings::HOME_BACK_NONE:
    default:
      break;
  }

  // Same rule as every list band (ListChrome.cpp): on a touch board Select names an action
  // on the black row only once a side key has shown one.
  const char* select = mappedInput.showsSelection() ? tr(STR_SELECT) : "";
  const auto labels = mappedInput.mapLabels(backLabel, select, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (cleanInitialRefresh) {
    // The panel still shows the previous screen (a page, a sleep image, a list), and a
    // FAST_REFRESH would leave it under the menu (upstream #3009). One-shot.
    cleanInitialRefresh = false;
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  } else {
    renderer.displayBuffer();
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

// A child of Home, so Back lands on the Library row again.
void HomeActivity::onLibraryOpen() {
  startActivityForResult(std::make_unique<LibraryActivity>(renderer, mappedInput), [](const ActivityResult&) {});
}

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() {
  activityManager.replaceActivity(
      std::make_unique<NearbyFileTransferActivity>(renderer, mappedInput, NearbyFileTransferActivity::Mode::Choose));
}

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

void HomeActivity::onBookOrbitMissingOpen() {
  const OpdsServer* server = bookorbit_missing::findServer();
  if (!server) return;
  activityManager.replaceActivity(std::make_unique<OpdsBookBrowserActivity>(
      renderer, mappedInput, *server, bookorbit_missing::savedFeedUrl(), /*missingOnly=*/true));
}
