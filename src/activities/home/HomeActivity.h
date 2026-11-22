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

  // The rows under the books, in order. Which of the optional ones show depends on the
  // card, the catalogs and the board, so onEnter builds the list once and every lookup
  // (count, row to item, item to row, label) reads it.
  static constexpr int MAX_MENU_ROWS = 7;
  HomeMenuItem menuRows[MAX_MENU_ROWS] = {};
  int menuRowsCount = 0;

  void buildMenuRows();
  int rowOfMenuItem(HomeMenuItem item) const;
  // A flick pages the books: the next screen of them, or the one before.
  void pageBooks(bool forward);
  void onSelectBook(const std::string& path);
  void onFileBrowserOpen();
  void onLibraryOpen();
  void onSettingsOpen();
  void onFileTransferOpen();
  void onOpdsBrowserOpen();
  void onBookOrbitMissingOpen();

  int getMenuItemCount() const;
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
