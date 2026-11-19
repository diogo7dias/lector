#pragma once

#include <LibraryFormat.h>

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Home > Library: every book on the card, by title with its author under it. Picking
// one opens it. The list is the on-card index (lib/LibraryIndex), so the screen holds
// only the rows on show: a window of titles and authors, never the library.
//
// The index is rebuilt (with a busy strip) on the first visit after boot, and on any
// later one after a book was added, moved, renamed or deleted: opening a book files it
// into /recents and finishing one into /read, and either changes the path the index
// holds. HalStorage counts those changes, so a row never names a file that is gone.
class LibraryActivity final : public UiListActivity {
  // Rows materialized per build: the visible page plus one clipped row, bounded so a
  // dense theme cannot size it. Strings only; the records behind them are re-read.
  static constexpr int WINDOW = 24;

  uint16_t count = 0;
  bool failed = false;

  std::string titles[WINDOW];
  std::string authors[WINDOW];
  library::ClixRecord records[WINDOW];
  std::vector<freeink::ui::ListItem> rows;
  mutable char counter[24] = "";

  void refreshIndex(bool force);
  void loadWindow(int start, int wanted);

 public:
  explicit LibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("Library", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;

 protected:
  int listCount() const override { return count; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  ListChrome chrome() const override;
};
