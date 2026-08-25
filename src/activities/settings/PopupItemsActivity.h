#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Ticks which actions the reader's Menu Pop-up lists. One row per entry in
// CrossPointSettings::POPUP_ITEM_FUNCTIONS, always in that order, so the pop-up rows and
// this screen can never disagree about what sits where — the tick order is not recorded
// anywhere and deliberately does not matter.
//
// At CrossPointSettings::POPUP_ITEM_MAX ticks the screen refuses further ones and says so
// rather than silently ignoring the press.
class PopupItemsActivity final : public UiListActivity {
 public:
  explicit PopupItemsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("PopupItems", renderer, mappedInput) {}

  void onExit() override;

 protected:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Header carries the "used / cap" counter, and the middle button ticks rather
  // than opens, so both bands are drawn here instead of by the base.
  void drawChrome() override;
  void drawFooter() override;

 private:
  // Row labels own their "[x] " prefix; the ListItems borrow these strings.
  std::vector<std::string> labels;
  std::vector<freeink::ui::ListItem> rows;
};
