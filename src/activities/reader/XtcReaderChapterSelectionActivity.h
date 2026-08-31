#pragma once
#include <Xtc.h>

#include <memory>
#include <vector>

#include "activities/UiListActivity.h"

class XtcReaderChapterSelectionActivity final : public UiListActivity {
  std::shared_ptr<Xtc> xtc;
  uint32_t currentPage = 0;

  // The rows; buildScreen only hands out pointers into the chapter list, so
  // the labels themselves live in the Xtc.
  std::vector<freeink::ui::ListItem> rows;

  int findChapterIndexForPage(uint32_t page) const;

 public:
  explicit XtcReaderChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const std::shared_ptr<Xtc>& xtc, uint32_t currentPage)
      : UiListActivity("XtcReaderChapterSelection", renderer, mappedInput), xtc(xtc), currentPage(currentPage) {}
  void onEnter() override;
  void onExit() override;

 protected:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  const char* headerTitle() const override { return tr(STR_SELECT_CHAPTER); }
};
