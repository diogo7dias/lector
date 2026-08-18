#pragma once
#include <functional>
#include <vector>

#include "./FileBrowserActivity.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct RecentBook;
struct Rect;

class HomeActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  // In-progress list scroll state: scrollOffset is the first index drawList starts
  // from; firstVisible/lastVisible are the range it actually rendered this frame
  // (variable row heights), used to keep the selected book on screen.
  int scrollOffset = 0;
  int firstVisibleBookIdx = 0;
  int lastVisibleBookIdx = 0;
  bool hasOpdsServers = false;
  std::vector<RecentBook> recentBooks;
  const HomeMenuItem initialMenuItem;
  // Cleared by the first render that consumes it, so only that paint pays for the
  // full-clear waveform.
  bool cleanInitialRefresh;

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

  int getMenuItemCount() const;
  // Menu rows only, excluding books and the pages tile.
  int menuRowCount() const;
  // Selector index of the resettable "pages read" tile in the header.
  // Header extras: the pages tile and, on boards with an RTC, the clock.
  void drawHomeHeaderExtras() const;
  void loadRecentBooks(int maxBooks);
  // Opens the Reading Stats screen for recentBooks[0], read straight off the card. Only
  // called with a non-empty recentBooks.
  void openRecentBookStats();

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        HomeMenuItem initialMenuItemValue = HomeMenuItem::NONE, bool cleanInitialRefresh = false)
      : Activity("Home", renderer, mappedInput),
        initialMenuItem(initialMenuItemValue),
        cleanInitialRefresh(cleanInitialRefresh) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }
};
