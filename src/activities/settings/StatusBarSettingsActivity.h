#pragma once
#include <cstdint>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Reader status bar configuration activity (v2 per-item model). Each text item is
// parked at one of six anchors (or Off) via a small in-place position picker; the
// progress bars, thickness and title/page sub-options cycle in place. A live
// preview of the real status bar is drawn at the bottom.
class StatusBarSettingsActivity final : public Activity {
 public:
  explicit StatusBarSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("StatusBarSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;

  int selectedIndex = 0;
  // The item ids that apply to this device (clock is X3-only), in display order.
  std::vector<int> visibleItems;

  // In-place anchor picker overlay. When active, up/down move pickerIndex over the
  // seven anchor choices (Off, TL, TC, TR, BL, BC, BR) and Confirm commits it to
  // *pickerTarget; Back cancels.
  bool pickerActive = false;
  int pickerIndex = 0;
  uint8_t* pickerTarget = nullptr;

  void handleSelection();
  // Rebuilds the display list. Two items are conditional: the clock (X3 RTC only) and
  // the hidden-bar progress row, which only makes sense while the status bar is off.
  // Called again whenever the master toggle flips so that row appears/disappears live.
  void rebuildVisibleItems();
  // Returns the SETTINGS anchor field for a position item, or nullptr for non-anchor items.
  uint8_t* anchorFieldFor(int itemId) const;
  void renderPicker();
};
