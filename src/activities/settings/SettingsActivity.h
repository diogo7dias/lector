#pragma once
#include <string>
#include <vector>

#include "SettingInfo.h"
#include "activities/UiGridActivity.h"
#include "components/OptionPopup.h"

class SettingsActivity final : public UiGridActivity {
  // The screen is either the category hub or one category's grid. 121 settings in one
  // flat list is 61 grid rows; split four ways, a category is one or two screens, and the
  // hub is what says which four there are now that the headings are gone.
  enum class Mode : uint8_t { Hub, Category };
  Mode mode = Mode::Hub;
  int selectedCategory = 0;
  int selectedSettingIndex = 0;
  int settingsCount = 0;

  // The cells the grid is drawing: one category's settings, its group headings dropped.
  std::vector<SettingInfo> settings;
  // Per-category scratch, kept as members so a rebuild reuses their capacity rather
  // than allocating four vectors of SettingInfo on every toggle.
  std::vector<SettingInfo> displaySettings;
  std::vector<SettingInfo> readerSettings;
  std::vector<SettingInfo> controlsSettings;
  std::vector<SettingInfo> systemSettings;

  OptionPopup optionPopup;

  // The two strings a cell is drawn from, rebuilt on demand: the base asks for
  // them one cell at a time and draws each immediately.
  mutable std::string cellNameScratch;
  mutable std::string cellValueScratch;

  void toggleCurrentSetting();
  // Puts the cursor back on a landable row after a rebuild that may have added or
  // removed rows under it.
  void restoreCursorAfterRebuild();
  // The field a live slider dialog is writing through while it is open, so its
  // static apply callback can reach it and Cancel can put it back. Null when no
  // dialog is up.
  uint8_t CrossPointSettings::* liveValuePtr = nullptr;
  void openSleepTimeoutPicker();
  /**
   * Writes this reader's WiFi networks and OPDS servers to a bundle on the card
   * and hands it to the Nearby sender. The bundle carries passwords in the clear
   * and the radio is not encrypted, so the other reader is asked before anything
   * moves and both ends delete the file afterwards.
   */
  void shareCredentials();
  void rebuildSettingsList();
  void startDownloadActivity(std::unique_ptr<Activity> activity);
  // The active category's rows, with its group headings dropped: a cell names itself, and
  // a heading band would cost a whole grid row to repeat what the order already says.
  void selectCategory(int index);
  std::vector<SettingInfo>& categoryRows(int index);
  StrId categoryName(int index) const;
  std::string settingValueText(const SettingInfo& setting) const;

 public:
  explicit SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiGridActivity(activity_name::kSettings, renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;

 protected:
  int cellCount() const override;
  const char* cellName(int index) const override;
  const char* cellValue(int index) const override;
  void activateCell(int index) override;
  ListChrome chrome() const override;
  bool handleCustomInput() override;
  void onBackButton() override;
  bool drawOverlay() override;
};
