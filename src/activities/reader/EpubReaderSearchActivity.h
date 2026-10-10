#pragma once
#include <Epub.h>
#include <Epub/SearchMatcher.h>

#include <string>
#include <vector>

#include "../UiListActivity.h"

// Searches the open book for a phrase, one chapter per loop pass so Back stays live, and lists
// the hits as they come. Picking one hands its chapter and text offset back to the reader,
// which lands on it the way it lands on a bookmark.
class EpubReaderSearchActivity final : public UiListActivity {
  struct Hit {
    int spineIndex;
    uint32_t offset;
    std::string snippet;
    std::string chapter;
  };

  // Enough to find the passage you meant; a word on every page would only fill the card's RAM.
  static constexpr size_t MAX_HITS = 100;

  Epub& epub;
  std::string query;
  SearchMatcher matcher;
  std::vector<Hit> hits;
  int nextSpine = 0;
  bool searching = true;
  mutable char progress[24] = {};  // header count, written by chrome()

  std::vector<freeink::ui::ListItem> rows;
  void searchNextChapter();
  static void onHit(void* ctx, uint32_t offset, const std::string& snippet);

 public:
  EpubReaderSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Epub& epub, std::string query)
      : UiListActivity("EpubReaderSearch", renderer, mappedInput), epub(epub), query(std::move(query)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;

 protected:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  ListChrome chrome() const override;
};
