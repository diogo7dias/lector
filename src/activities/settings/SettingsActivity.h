#pragma once
#include <memory>
#include <string>
#include <vector>

#include "SettingInfo.h"
#include "activities/UiGridActivity.h"
#include "components/OptionPopup.h"

class SettingsActivity final : public UiGridActivity {
  // The screen is either the hub (five summary cards) or one category's rows under their
  // group headings. System's rare rows sit one level down, in Maintenance.
  enum class Mode : uint8_t { Hub, Category };
  Mode mode = Mode::Hub;
  int openHubCell = 0;
  int openCategory = -1;
  SettingAction openRow = SettingAction::None;
  int selectedCategory = 0;
  int settingsCount = 0;

  // The cells the grid is drawing: one category's settings and its group headings.
  std::vector<SettingInfo> settings;
  // Per-category scratch, kept as members so a rebuild reuses their capacity rather
  // than allocating four vectors of SettingInfo on every toggle.
  std::vector<SettingInfo> displaySettings;
  std::vector<SettingInfo> readerSettings;
  std::vector<SettingInfo> controlsSettings;
  std::vector<SettingInfo> systemSettings;
  // System > Maintenance: storage clean-up, the reading cache and the firmware rows.
  std::vector<SettingInfo> maintenanceSettings;

  OptionPopup optionPopup;

  // The two strings a cell is drawn from, rebuilt on demand: the base asks for
  // them one cell at a time and draws each immediately.
  mutable std::string cellNameScratch;
  mutable std::string cellValueScratch;

  void toggleCurrentSetting();
  // Puts the cursor back on a landable row after a rebuild that may have added or
  // removed rows under it.
  void restoreCursorAfterRebuild();
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
  // Loads a category's rows (Maintenance included) into `settings`.
  void selectCategory(int index);
  std::vector<SettingInfo>& categoryRows(int index);
  StrId categoryName(int index) const;
  std::string settingValueText(const SettingInfo& setting) const;
  // The line under a hub card's name: two live values of that category
  // (SettingsHubSummary.cpp).
  std::string hubSummary(int index) const;

 public:
  // Opens on the hub's first card. A screen that replaced Settings comes back to the place it
  // was opened from: a hub card (File Transfer), or a row inside a category.
  explicit SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int hubCell = 0, int category = -1,
                            SettingAction row = SettingAction::None)
      : UiGridActivity(activity_name::kSettings, renderer, mappedInput),
        openHubCell(hubCell),
        openCategory(category),
        openRow(row) {}
  // The places a screen that replaced Settings comes back to (ActivityManager::Opener).
  static std::unique_ptr<SettingsActivity> onFileTransferCard(GfxRenderer& renderer, MappedInputManager& mappedInput);
  static std::unique_ptr<SettingsActivity> onShareCredentials(GfxRenderer& renderer, MappedInputManager& mappedInput);
  // Rebuilt after a silent restart on the row restartStep() saved.
  static std::unique_ptr<SettingsActivity> fromRestartRow(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                          int row);
  bool restartStep(uint8_t& opener, int& row) const override;
  void onEnter() override;
  void onExit() override;

 protected:
  int cellCount() const override;
  CellLook cellLook(int index) const override;
  const char* cellName(int index) const override;
  const char* cellValue(int index) const override;
  void activateCell(int index) override;
  ListChrome chrome() const override;
  bool handleCustomInput() override;
  void onBackButton() override;
  bool drawOverlay() override;
};
