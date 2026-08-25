#pragma once

#ifdef LECTOR_LOCK_LAB_UI

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// The Lock Lab screen: one row per knob plus a Run row.
//
// Confirm on a knob row steps that knob; Confirm on the Run row renders a wallpaper with
// the current settings and replaces the list with the result until a button is pressed.
// See src/dev/LockLab.h for why this screen exists and why its strings are literals.
class LockLabActivity final : public UiListActivity {
 public:
  explicit LockLabActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("LockLab", renderer, mappedInput) {}

  void onExit() override;

 protected:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override { return "Lock Lab"; }
  bool handleCustomInput() override;

 private:
  // Row values own their strings; the ListItems borrow them.
  std::vector<std::string> values;
  std::vector<freeink::ui::ListItem> rows;

  // The last run's one-line summary, shown over the rendered wallpaper until a button is
  // pressed. Empty means the list is on screen.
  std::string result;
};

#endif  // LECTOR_LOCK_LAB_UI
