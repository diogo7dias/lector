#pragma once
#include <functional>
#include <vector>

#include "./FileBrowserActivity.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/Sortes.h"

struct RecentBook;
struct Rect;

class HomeActivity final : public Activity {
  ButtonNavigator buttonNavigator{ButtonNavigator::LIST_REPEAT_INTERVAL_MS, ButtonNavigator::LIST_REPEAT_START_MS};
  int selectorIndex = 0;
  // In-progress list scroll state: scrollOffset is the first index drawList starts
  // from; firstVisible/lastVisible are the range it actually rendered this frame
  // (variable row heights), used to keep the selected book on screen.
  int scrollOffset = 0;
  int firstVisibleBookIdx = 0;
  int lastVisibleBookIdx = 0;
  bool hasOpdsServers = false;
  // Books reading on BookOrbit that the card lacks, from the last sleep check.
  int bookOrbitMissing = 0;
  char bookOrbitMissingLabel[64] = "";
  std::string sortesBook;
  sortes::ScanResult sortesResult = sortes::ScanResult::Empty;
  std::vector<RecentBook> recentBooks;
  const HomeMenuItem initialMenuItem;
  // Home is reached from the reader, a sleep face or a list: its first paint cleans
  // whatever was there. Cleared by that render, so later paints stay FAST.
  bool cleanInitialRefresh = true;

  // Convert HomeMenuItem to menu index (used in onEnter)
  static int menuItemToIndex(HomeMenuItem item, bool hasOpdsUrl, bool hasMissing) {
    int i = 0;
    if (item == HomeMenuItem::BOOKORBIT_MISSING) return 0;
    if (hasMissing) ++i;
    if (item == HomeMenuItem::FILE_BROWSER) return i;
    ++i;
    if (item == HomeMenuItem::OPDS_BROWSER) return hasOpdsUrl ? i : 0;
    if (hasOpdsUrl) ++i;
    if (item == HomeMenuItem::FILE_TRANSFER) return i;
    ++i;
    if (item == HomeMenuItem::STATS) return i;
    ++i;
    if (item == HomeMenuItem::SETTINGS_MENU) return i;
    return 0;
  }

  // Convert menu index to HomeMenuItem (used in loop)
  static HomeMenuItem indexToMenuItem(int idx, bool hasOpdsUrl, bool hasMissing) {
    int i = 0;
    if (hasMissing && idx == i++) return HomeMenuItem::BOOKORBIT_MISSING;
    if (idx == i++) return HomeMenuItem::FILE_BROWSER;
    if (hasOpdsUrl && idx == i++) return HomeMenuItem::OPDS_BROWSER;
    if (idx == i++) return HomeMenuItem::FILE_TRANSFER;
    if (idx == i++) return HomeMenuItem::STATS;
    if (idx == i) return HomeMenuItem::SETTINGS_MENU;
    return HomeMenuItem::NONE;
  }
  void onSelectBook(const std::string& path);
  void onFileBrowserOpen();
  void onSettingsOpen();
  void onStatsOpen();
  void onFileTransferOpen();
  void onOpdsBrowserOpen();
  void onBookOrbitMissingOpen();

  int getMenuItemCount() const;
  // Menu rows only, excluding books.
  int menuRowCount() const;
  void loadRecentBooks(int maxBooks);

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        HomeMenuItem initialMenuItemValue = HomeMenuItem::NONE)
      : Activity(activity_name::kHome, renderer, mappedInput), initialMenuItem(initialMenuItemValue) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }
};
