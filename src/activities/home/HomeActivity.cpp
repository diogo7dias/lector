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

#include <algorithm>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "activities/reader/BookStatsActivity.h"
#include "components/BusyBanner.h"
#include "components/UITheme.h"
#include "components/icons/skull12.h"
#include "fontIds.h"
#include "reading_stats/ReaderStatsSession.h"
#include "reading_stats/ReadingStatsPresentation.h"
#include "reading_stats/ReadingStatsStore.h"
#include "reading_stats/SdStatsFiles.h"
#include "util/BookCacheUtils.h"
#include "util/BusyTick.h"
#include "util/DeferredFavorite.h"

int HomeActivity::menuRowCount() const {
  int count = 3;  // File Browser, File transfer, Settings
  if (hasOpdsServers) {
    count++;
  }
#ifdef LECTOR_LOCK_LAB_UI
  count++;  // Lock Lab, kit builds only
#endif
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

    // One SD stat per book, and a full store on a slow card is where a home press
    // spends its seconds.
    busy::tick();

    // Skip if file no longer exists
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
#ifdef LECTOR_LOCK_LAB_UI
      case HomeMenuItem::LOCK_LAB:
        onLockLabOpen();
        break;
#endif
      default:
        break;
    }
  };

  // A tap picks the row it landed on and acts on it in one go — the selection moving
  // first is what the paint after the action shows, so no extra refresh is spent on it.
  int tappedItem = 0;
  if (mappedInput.wasRowTapped(tappedItem) && tappedItem >= 0 && tappedItem < menuCount) {
    selectorIndex = tappedItem;
    activateSelection();
    return;
  }

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
      case CrossPointSettings::HOME_BACK_STATS:
        if (!recentBooks.empty()) {
          openRecentBookStats();
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

void HomeActivity::openRecentBookStats() {
  const RecentBook& book = recentBooks[0];
  const std::string cacheDir = bookCacheDirForPath(book.path);
  if (cacheDir.empty()) return;  // an extension no reader caches has no stats file either

  // Read the two stats files directly. The home screen has no ReaderStatsSession, so these
  // are the numbers the last reading session saved and nothing is being timed right now.
  // A missing file loads as an all-zero record, which the screen renders as "no reading
  // yet" rather than failing.
  reading_stats::SdStatsFiles files;
  reading_stats::ReadingStatsStore store(files);
  reading_stats::ReadingStatsData bookStats;
  reading_stats::ReadingStatsData globalStats;
  store.load(cacheDir + "/reading_stats.bin", bookStats);
  store.load(reading_stats::ReaderStatsSession::globalPath(), globalStats);

  const uint8_t progress = book.progressPercent > 0 ? static_cast<uint8_t>(book.progressPercent) : 0;

  startActivityForResult(std::make_unique<BookStatsActivity>(
                             renderer, mappedInput, book.title, bookStats, globalStats, progress,
                             reading_stats::estimateTimeLeft(bookStats.totalReadingSeconds, progress),
                             // Reset writes straight back to the same two files. Nothing here holds a live
                             // session, so there is no in-memory copy that could overwrite it afterwards.
                             [cacheDir](const bool resetAll, reading_stats::ReadingStatsData& outBook,
                                        reading_stats::ReadingStatsData& outGlobal) {
                               reading_stats::SdStatsFiles resetFiles;
                               reading_stats::ReadingStatsStore resetStore(resetFiles);
                               outBook = reading_stats::ReadingStatsData{};
                               if (!resetStore.reset(cacheDir + "/reading_stats.bin", outBook)) return false;
                               if (resetAll) {
                                 outGlobal = reading_stats::ReadingStatsData{};
                                 if (!resetStore.reset(reading_stats::ReaderStatsSession::globalPath(), outGlobal))
                                   return false;
                               } else {
                                 resetStore.load(reading_stats::ReaderStatsSession::globalPath(), outGlobal);
                               }
                               return true;
                             }),
                         [this](const ActivityResult&) { requestUpdate(); });
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
      halClock.isAvailable() &&
      halClock.formatTime(timeBuf, sizeof(timeBuf), SETTINGS.clockUtcOffsetQ, SETTINGS.clockFormat == 1);
  GUI.drawHomeHeaderExtras(renderer, CROSSPOINT_VERSION, hasClock ? timeBuf : nullptr);

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

#ifdef LECTOR_LOCK_LAB_UI
  // Appended after Settings, and after the OPDS insert above, so the row order every
  // other index in this file assumes is untouched. Literal rather than tr(): the lab is
  // never in a release build and the generated string tables are not #ifdef-aware, so a
  // key for it would cost flash in shipped firmware.
  menuItems.push_back("Lock Lab");
  menuIcons.push_back(Settings);
#endif

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
      [&menuIcons](int index) { return menuIcons[index]; },
      // The books above own indices 0..N-1 of the same selection space the menu continues.
      metrics.homeContinueReadingInMenu ? 0 : static_cast<int>(recentBooks.size()));

  // Back's hint must match what it actually does. An empty label draws no
  // button box at all, which is what HOME_BACK_NONE wants.
  const char* backLabel = "";
  switch (SETTINGS.homeBackAction) {
    case CrossPointSettings::HOME_BACK_RESUME:
      backLabel = recentBooks.empty() ? "" : tr(STR_RESUME);
      break;
    case CrossPointSettings::HOME_BACK_STATS:
      // "Stats", not "Reading Stats": a button hint has one small box to live in, and the
      // longer wording was the only label in the firmware wide enough to need wrapping.
      // The in-book menu row keeps the full name, where there is room for it.
      backLabel = recentBooks.empty() ? "" : tr(STR_STATS);
      break;
    case CrossPointSettings::HOME_BACK_NONE:
    default:
      break;
  }

  const auto labels = mappedInput.mapLabels(backLabel, tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (cleanInitialRefresh) {
    // Splashless wake with a custom sleep face and no saved frame: the panel still
    // physically shows the sleep image, and a FAST_REFRESH would leave it under the menu.
    // One HALF_REFRESH on this first paint clears it without a second refresh pass
    // (upstream #3009). One-shot: later Home paints go back to the cheap path.
    cleanInitialRefresh = false;
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  } else {
    renderer.displayBuffer();
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

#ifdef LECTOR_LOCK_LAB_UI
void HomeActivity::onLockLabOpen() { activityManager.goToLockLab(); }
#endif

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }
