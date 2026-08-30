#pragma once
#include <cstdint>
#include <vector>

#include <string>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// Reader status bar configuration activity (v2 per-item model). Each text item is
// parked at one of six anchors (or Off) via a small in-place position picker; the
// progress bars, thickness and title/page sub-options cycle in place. A live
// preview of the real status bar is drawn at the bottom.
class StatusBarSettingsActivity final : public UiListActivity {
 public:
  explicit StatusBarSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("StatusBarSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;

 protected:
  int listCount() const override { return static_cast<int>(visibleItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override { return tr(STR_CUSTOMISE_STATUS_BAR); }
  // The anchor picker owns every button while it is up, so it runs before the base
  // touches Back, Confirm or the selection.
  bool handleCustomInput() override;
  bool drawOverlay() override;

 private:
  // The item ids that apply to this device (clock is X3-only), in display order.
  std::vector<int> visibleItems;

  // The anchor picker: the seven choices (Off, TL, TC, TR, BL, BC, BR) as the
  // shared popup, so it looks like every other choice in the firmware instead of
  // like a box this screen drew for itself.
  OptionPopup anchorPopup;

  void handleSelection(int index);
  // Rebuilds the display list. Two items are conditional: the clock (X3 RTC only) and
  // the hidden-bar progress row, which only makes sense while the status bar is off.
  // Called again whenever the master toggle flips so that row appears/disappears live.
  void rebuildVisibleItems();
  // Returns the SETTINGS anchor field for a position item, or nullptr for non-anchor items.
  uint8_t* anchorFieldFor(int itemId) const;
  // The value column text for one item id.
  std::string rowValue(int id) const;

  // Row text owns its strings; the ListItems borrow them.
  std::vector<std::string> subtitles;
  std::vector<freeink::ui::ListItem> rows;
};
