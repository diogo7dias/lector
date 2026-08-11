#pragma once

#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Ticks which actions the reader's Menu Pop-up lists. One row per entry in
// CrossPointSettings::POPUP_ITEM_FUNCTIONS, always in that order, so the pop-up rows and
// this screen can never disagree about what sits where — the tick order is not recorded
// anywhere and deliberately does not matter.
//
// At CrossPointSettings::POPUP_ITEM_MAX ticks the screen refuses further ones and says so
// rather than silently ignoring the press.
class PopupItemsActivity final : public Activity {
 public:
  explicit PopupItemsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("PopupItems", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  void toggleSelected();
};
