#pragma once
#include <string>
#include <vector>

#include "./FileBrowserActivity.h"
#include "RecentBooksStore.h"
#include "activities/UiListActivity.h"
#include "util/Sortes.h"

// Home as a contents page: the in-progress books under one heading, the places to go
// under another, the device's clock and battery at the foot.
class HomeActivity final : public UiListActivity {
  bool hasOpdsServers = false;
  std::string sortesBook;
  sortes::ScanResult sortesResult = sortes::ScanResult::Empty;
  std::vector<RecentBook> recentBooks;
  const HomeMenuItem initialMenuItem;
  // Cleared by the first render that consumes it, so only that paint pays for the
  // full-clear waveform.
  bool cleanInitialRefresh;
  // What the rows borrow: the author line and the progress of each book, the folio.
  std::vector<freeink::ui::ListItem> rows;
  std::vector<std::string> authors;
  std::vector<std::string> percents;
  mutable std::string folio;

  // Convert HomeMenuItem to menu index (used in onEnter)
  static int menuItemToIndex(HomeMenuItem item, bool hasOpdsUrl) {
    int i = 0;
    if (item == HomeMenuItem::FILE_BROWSER) return i;
    ++i;
    if (item == HomeMenuItem::OPDS_BROWSER) return hasOpdsUrl ? i : 0;
    if (hasOpdsUrl) ++i;
    if (item == HomeMenuItem::FILE_TRANSFER) return i;
    ++i;
    if (item == HomeMenuItem::SETTINGS_MENU) return i;
    return 0;
  }

  // Convert menu index to HomeMenuItem (used in loop)
  static HomeMenuItem indexToMenuItem(int idx, bool hasOpdsUrl) {
    int i = 0;
    if (idx == i++) return HomeMenuItem::FILE_BROWSER;
    if (hasOpdsUrl && idx == i++) return HomeMenuItem::OPDS_BROWSER;
    if (idx == i++) return HomeMenuItem::FILE_TRANSFER;
    if (idx == i) return HomeMenuItem::SETTINGS_MENU;
    return HomeMenuItem::NONE;
  }
  void onSelectBook(const std::string& path);
  void onFileBrowserOpen();
  void onSettingsOpen();
  void onFileTransferOpen();
  void onOpdsBrowserOpen();

  int menuRowCount() const;
  // What Back does under Sortes, as its hint and as the toast when there is nothing to open.
  const char* sortesHint() const;
  void loadRecentBooks(int maxBooks);
  // Row layout: a heading over the books when there are any, then a heading over the menu.
  int bookCount() const { return static_cast<int>(recentBooks.size()); }
  int firstBookRow() const { return 1; }
  int libraryHeadingRow() const { return bookCount() > 0 ? bookCount() + 1 : 0; }
  int firstMenuRow() const { return libraryHeadingRow() + 1; }

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        HomeMenuItem initialMenuItemValue = HomeMenuItem::NONE, bool cleanInitialRefresh = false)
      : UiListActivity(activity_name::kHome, renderer, mappedInput),
        initialMenuItem(initialMenuItemValue),
        cleanInitialRefresh(cleanInitialRefresh) {}
  void onEnter() override;
  bool isHomeActivity() const override { return true; }

 protected:
  int listCount() const override { return firstMenuRow() + menuRowCount(); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool isHeaderRow(int index) const override { return index == libraryHeadingRow() || (bookCount() > 0 && index == 0); }
  void onBackButton() override;
  ListChrome chrome() const override;
  HalDisplay::RefreshMode refreshMode() override;
};
