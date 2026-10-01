#pragma once
#include <Xtc.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

class XtcReaderChapterSelectionActivity final : public UiListActivity {
  Xtc& xtc;
  uint32_t currentPage = 0;

  // The rows; buildScreen only hands out pointers into the chapter list, so
  // the labels themselves live in the Xtc.
  std::vector<freeink::ui::ListItem> rows;
  // The title page's italic line; chrome() borrows it.
  std::string bookTitle;

  int findChapterIndexForPage(uint32_t page) const;

 public:
  explicit XtcReaderChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Xtc& xtc,
                                             uint32_t currentPage)
      : UiListActivity("XtcReaderChapterSelection", renderer, mappedInput), xtc(xtc), currentPage(currentPage) {}
  void onEnter() override;
  void onExit() override;

 protected:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  ListChrome chrome() const override {
    ListChrome chrome;
    chrome.title = tr(STR_SELECT_CHAPTER);
    chrome.subHeader = bookTitle.c_str();
    return chrome;
  }
};
