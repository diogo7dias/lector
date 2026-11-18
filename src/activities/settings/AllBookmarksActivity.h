#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "../../BookmarkEntry.h"
#include "../UiListActivity.h"

// Settings > Reader > All Bookmarks: every book's bookmarks in one list. Picking one
// opens that book at it. Deleting stays in the book's own Bookmarks list.
class AllBookmarksActivity final : public UiListActivity {
  struct Mark {
    uint16_t book;  // index into books and titles
    BookmarkEntry entry;
  };
  std::vector<std::string> books;
  std::vector<std::string> titles;
  std::vector<Mark> marks;

  // The rows and the strings behind them: buildScreen only hands out pointers,
  // so both have to outlive it.
  std::vector<freeink::ui::ListItem> rows;
  std::vector<std::string> subtitles;

  void load();

 public:
  explicit AllBookmarksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("AllBookmarks", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;

 protected:
  int listCount() const override { return static_cast<int>(marks.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  ListChrome chrome() const override;
};
