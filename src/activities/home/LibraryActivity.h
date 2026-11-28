#pragma once

#include <LibraryFormat.h>

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "activities/home/LibraryCoverGrid.h"
#include "activities/home/LibrarySearch.h"

// Home > Library: every book on the card, by title with its author under it. Picking
// one opens it. The list is the on-card index (lib/LibraryIndex), so the screen holds
// only the rows on show: a window of titles and authors, never the library.
//
// The index is rebuilt (with a busy strip) on the first visit after boot, and on any
// later one after a book was added, moved, renamed or deleted: opening a book files it
// into /recents and finishing one into /read, and either changes the path the index
// holds. HalStorage counts those changes, so a row never names a file that is gone.
//
// The first row searches every book on the card: type, OK, and the list becomes the
// best matches by title or author. Not live: one scan of the index per OK.
class LibraryActivity final : public UiListActivity {
  // Rows materialized per build: the visible page plus one clipped row, bounded so a
  // dense theme cannot size it. Strings only; the records behind them are re-read.
  static constexpr int WINDOW = 24;
  // Search results kept; a broad query on a big card matches far more than anyone
  // scrolls through, and the fixed array keeps the scan from allocating per hit.
  static constexpr size_t MAX_HITS = 64;

  uint16_t bookCount = 0;
  // Rows the list may show; loadWindow shortens it when the index reads short.
  int rowCount = 0;
  bool failed = false;
  // The row to open on: a book's Back returns to the row that opened it.
  uint16_t startRow = 0;

  std::string searchQuery;
  librarysearch::TopMatches<MAX_HITS> hits;
  // Every match, kept or not, for the "N found" line.
  int totalHits = 0;
  mutable char hitsLine[24] = "";
  std::string searchLabel;

  std::string titles[WINDOW];
  std::string authors[WINDOW];
  library::ClixRecord records[WINDOW];
  std::vector<freeink::ui::ListItem> rows;
  mutable char counter[24] = "";

  // Cover Grid mode state (active when SETTINGS.libraryViewMode != 0).
  int gridSelected = 0;
  int gridVisibleCount = 0;
  bool gridCleanPaint = true;
  library_grid::PageLayout gridLayout{};
  library_grid::CoverGridRenderer gridRenderer;

  bool isGridMode() const;
  bool searchActive() const { return !searchQuery.empty(); }
  // Search row, then Clear search while a query is set.
  int headerRows() const { return bookCount == 0 ? 0 : (searchActive() ? 2 : 1); }
  int entryCount() const { return searchActive() ? static_cast<int>(hits.size()) : bookCount; }
  // Index ordinal of the book on entry row `entry`.
  int ordinalAt(int entry) const { return searchActive() ? hits.indexAt(static_cast<size_t>(entry)) : entry; }
  void recount() { rowCount = headerRows() + entryCount(); }

  void refreshIndex(bool force);
  // Reads `wanted` entries from entry row `first` into titles/authors.
  int loadEntries(int first, int wanted);
  void openEntry(int entryIndex);
  void openSearch();
  void applySearch(const std::string& query);
  void renderGrid();

 public:
  explicit LibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, uint16_t firstRow = 0)
      : UiListActivity(activity_name::kLibrary, renderer, mappedInput), startRow(firstRow) {}
  void onEnter() override;
  void onExit() override;
  void render(RenderLock&& lock) override;
  void onRevealed() override {
    UiListActivity::onRevealed();
    gridCleanPaint = true;
  }
  bool lightPanelAuxText(char* buf, size_t size) const override;
  bool lightPanelStepAux(int delta) override;

 protected:
  int listCount() const override { return rowCount; }
  void buildScreen(UiScreen& screen) override;
  bool handleCustomInput() override;
  void activateIndex(int index) override;
  void onBackButton() override;
  ListChrome chrome() const override;
};
