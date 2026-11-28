#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/ActivityNames.h"
#include "activities/UiListActivity.h"

// Displays in-book search hits for EPUB (chapter + snippet) or TXT (page + snippet).
// Confirm or tap returns a ProgressChangeResult with the hit's spineIndex and offset;
// Back cancels cleanly.
class BookSearchResultsActivity final : public UiListActivity {
 public:
  struct ResultItem {
    int spineIndex = 0;
    uint32_t offset = 0;
    std::string label;    // chapter title (EPUB) or "Page N" (TXT)
    std::string snippet;  // clean UTF-8 context around the match
  };

  BookSearchResultsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string query,
                            std::vector<ResultItem> items, bool truncated)
      : UiListActivity(activity_name::kBookSearchResults, renderer, mappedInput),
        query(std::move(query)),
        items(std::move(items)),
        truncated(truncated) {}

  void onEnter() override;
  void onExit() override;

 protected:
  int listCount() const override { return static_cast<int>(items.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  ListChrome chrome() const override;

 private:
  std::string query;
  std::vector<ResultItem> items;
  bool truncated = false;

  std::vector<freeink::ui::ListItem> rows;
  std::vector<std::string> labelTags;
  mutable std::string headerText;
  mutable std::string countText;
};
