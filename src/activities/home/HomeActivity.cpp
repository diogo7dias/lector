#include "HomeActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <esp_random.h>

#include <algorithm>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "activities/network/NearbyFileTransferActivity.h"
#include "components/BusyBanner.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BusyTick.h"
#include "util/DeferredFavorite.h"
#include "util/StringUtils.h"

namespace fui = freeink::ui;

int HomeActivity::menuRowCount() const {
  int count = 3;  // File Browser, File transfer, Settings
  if (hasOpdsServers) {
    count++;
  }
  return count;
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
  if (SETTINGS.homeBackAction == CrossPointSettings::HOME_BACK_SORTES) {
    BusyBanner banner(renderer, tr(STR_SORTES));
    sortesResult = sortes::findBook(sortesBook, esp_random);
  }

  // Every recent (in-progress) book, up to the store cap; the list windows them.
  loadRecentBooks(RecentBooksStore::MAX_RECENT_BOOKS);
  authors.clear();
  percents.clear();
  authors.reserve(recentBooks.size());
  percents.reserve(recentBooks.size());
  for (const RecentBook& book : recentBooks) {
    authors.push_back(SETTINGS.authorDisplay == CrossPointSettings::AUTHOR_FULL_NAME
                          ? book.author
                          : StringUtils::authorInitials(book.author));
    char pct[8] = "";
    if (book.progressPercent >= 0) snprintf(pct, sizeof(pct), "%d%%", book.progressPercent);
    percents.emplace_back(pct);
  }

  UiListActivity::onEnter();
  const int first = bookCount() > 0 ? firstBookRow() : firstMenuRow();
  moveSelectionTo(initialMenuItem == HomeMenuItem::NONE
                      ? first
                      : firstMenuRow() + menuItemToIndex(initialMenuItem, hasOpdsServers));
}

void HomeActivity::buildScreen(UiScreen& screen) {
  static const char* const MORE = "\xE2\x80\xBA";
  rows.assign(listCount(), fui::ListItem{});
  if (bookCount() > 0) {
    rows[0].label = tr(STR_CONTINUE_READING);
    rows[0].isHeader = true;
  }
  for (int i = 0; i < bookCount(); ++i) {
    auto& row = rows[firstBookRow() + i];
    row.label = recentBooks[i].title.c_str();
    row.subtitle = authors[i].empty() ? nullptr : authors[i].c_str();
    row.value = percents[i].empty() ? nullptr : percents[i].c_str();
  }
  rows[libraryHeadingRow()].label = tr(STR_GRP_LIBRARY);
  rows[libraryHeadingRow()].isHeader = true;
  for (int i = 0; i < menuRowCount(); ++i) {
    auto& row = rows[firstMenuRow() + i];
    switch (indexToMenuItem(i, hasOpdsServers)) {
      case HomeMenuItem::FILE_BROWSER:
        row.label = tr(STR_BROWSE_FILES);
        break;
      case HomeMenuItem::OPDS_BROWSER:
        row.label = tr(STR_OPDS_BROWSER);
        break;
      case HomeMenuItem::FILE_TRANSFER:
        row.label = tr(STR_NEARBY_SYNC);
        break;
      default:
        row.label = tr(STR_SETTINGS_TITLE);
        break;
    }
    row.value = MORE;
  }
  for (int i = 0; i < listCount(); ++i) rows[i].actionValue = static_cast<int16_t>(i);

  fui::ListProps props{};
  props.items = rows.data();
  props.count = static_cast<uint16_t>(rows.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props, /*hasSubtitle=*/bookCount() > 0);
  screen.list(props);
}

void HomeActivity::activateIndex(const int index) {
  if (isHeaderRow(index)) return;
  if (index < firstMenuRow()) {
    onSelectBook(recentBooks[index - firstBookRow()].path);
    return;
  }
  switch (indexToMenuItem(index - firstMenuRow(), hasOpdsServers)) {
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
}

// Back is otherwise unused on the home menu, so it runs the user's configured action.
void HomeActivity::onBackButton() {
  switch (SETTINGS.homeBackAction) {
    case CrossPointSettings::HOME_BACK_RESUME:
      // recentBooks is most-recent-first and already pruned of files missing from the SD card.
      if (!recentBooks.empty()) onSelectBook(recentBooks[0].path);
      break;
    case CrossPointSettings::HOME_BACK_SORTES:
      if (sortesResult == sortes::ScanResult::Found) {
        BusyBanner banner(renderer, tr(STR_SORTES));
        sortesResult = sortes::findBook(sortesBook, esp_random);
        if (sortesResult == sortes::ScanResult::Found) {
          activityManager.goToReader(sortesBook, false, false, true);
          break;
        }
      }
      // Keys-only boards carry no hint band to say why Back did nothing, so the press says it.
      GUI.drawPopup(renderer, sortesHint());
      break;
    default:
      break;
  }
}

const char* HomeActivity::sortesHint() const {
  return sortesResult == sortes::ScanResult::Found   ? tr(STR_SORTES)
         : sortesResult == sortes::ScanResult::Empty ? tr(STR_SORTES_EMPTY)
                                                     : tr(STR_SORTES_UNAVAILABLE);
}

// The title page: the firmware's name and version, the clock and the battery at the foot.
ListChrome HomeActivity::chrome() const {
  ListChrome chrome;
  chrome.title = tr(STR_LECTOR);
  chrome.subHeader = CROSSPOINT_VERSION;
  char timeBuf[9];
  const bool hasClock =
      halClock.isAvailable() &&
      halClock.formatTime(timeBuf, sizeof(timeBuf), SETTINGS.clockUtcOffsetQ, SETTINGS.clockFormat == 1);
  char foot[32];
  if (hasClock) {
    snprintf(foot, sizeof(foot), "%s \xC2\xB7 %u%%", timeBuf, powerManager.getBatteryPercentage());
  } else {
    snprintf(foot, sizeof(foot), "%u%%", powerManager.getBatteryPercentage());
  }
  folio = foot;
  chrome.folio = folio.c_str();
  // Back's hint says what it does; an empty label draws no box, which is what NONE wants.
  switch (SETTINGS.homeBackAction) {
    case CrossPointSettings::HOME_BACK_RESUME:
      chrome.backHint = recentBooks.empty() ? "" : tr(STR_RESUME);
      break;
    case CrossPointSettings::HOME_BACK_SORTES:
      chrome.backHint = sortesHint();
      break;
    default:
      chrome.backHint = "";
      break;
  }
  return chrome;
}

// Splashless wake with a custom sleep face and no saved frame: the panel still physically
// shows the sleep image, and a FAST_REFRESH would leave it under the menu. One HALF_REFRESH
// on this first paint clears it (upstream #3009); later paints go back to the cheap path.
HalDisplay::RefreshMode HomeActivity::refreshMode() {
  if (!cleanInitialRefresh) return HalDisplay::FAST_REFRESH;
  cleanInitialRefresh = false;
  return HalDisplay::HALF_REFRESH;
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() {
  activityManager.replaceActivity(
      std::make_unique<NearbyFileTransferActivity>(renderer, mappedInput, NearbyFileTransferActivity::Mode::Choose));
}

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }
