#pragma once
#include <I18n.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "components/SettingsGrid.h"
#include "components/ValueBarPopup.h"
#include "util/ButtonNavigator.h"

enum class SettingType { TOGGLE, ENUM, ACTION, VALUE, STRING };

enum class SettingAction {
  None,
  RemapFrontButtons,
  CustomiseStatusBar,
  KOReaderSync,
  OPDSBrowser,
  Network,
  ClearCache,
  CleanStorage,
  ShuffleWallpapers,
  CheckForUpdates,
  InstallOtherFirmware,
  SdFirmwareUpdate,
  Language,
  DownloadFonts,
  InstalledFonts,
  TextSettings,
  PopupItems,
  Buttons,
  ShareCredentials,
};

struct SettingInfo {
  StrId nameId;
  SettingType type;
  uint8_t CrossPointSettings::* valuePtr = nullptr;
  std::vector<StrId> enumValues;
  std::vector<std::string> enumStringValues;  // runtime alternative to StrId enumValues (for SD card fonts etc.)
  SettingAction action = SettingAction::None;

  struct ValueRange {
    uint8_t min;
    uint8_t max;
    uint8_t step;
  };
  ValueRange valueRange = {};

  const char* key = nullptr;             // JSON API key (nullptr for ACTION types)
  StrId category = StrId::STR_NONE_OPT;  // Category for web UI grouping
  // Section heading rather than a setting: drawn as a label with a rule, never
  // selectable. Built only by SettingsActivity::rebuildSettingsList, never by
  // getSettingsList, so headings stay out of JSON persistence and the web API.
  bool isHeader = false;
  bool obfuscated = false;      // Save/load via base64 obfuscation (passwords)
  bool inTextSettings = false;  // Surfaced in the Text Settings screen; hidden from the flat Reader list
  // Surfaced in the Buttons screen; hidden from the flat Controls list. The eighteen
  // per-button bindings stay in the shared list so they persist and reach the web API,
  // but eighteen rows in Controls would bury every other control there.
  bool inButtons = false;

  // Enum values that are no longer offered but must keep their slot. ENUM settings persist
  // by index, so a retired option cannot simply be deleted from enumValues without
  // reinterpreting every value after it. Listing it here drops it from the picker while
  // enumValues stays index-aligned with the enum. Stored values are migrated away in
  // CrossPointSettings::fromJson, so a hidden value should never be the current one.
  std::vector<uint8_t> hiddenEnumValues;

  bool isEnumValueHidden(const uint8_t value) const {
    return std::find(hiddenEnumValues.begin(), hiddenEnumValues.end(), value) != hiddenEnumValues.end();
  }

  // Direct char[] string fields (for settings stored in CrossPointSettings)
  size_t stringOffset = 0;
  size_t stringMaxLen = 0;

  // Dynamic accessors (for settings stored outside CrossPointSettings, e.g. KOReaderCredentialStore)
  std::function<uint8_t()> valueGetter;
  std::function<void(uint8_t)> valueSetter;
  std::function<std::string()> stringGetter;
  std::function<void(const std::string&)> stringSetter;

  SettingInfo& withObfuscated() {
    obfuscated = true;
    return *this;
  }

  SettingInfo& withTextSettings() {
    inTextSettings = true;
    return *this;
  }

  SettingInfo& withButtons() {
    inButtons = true;
    return *this;
  }

  SettingInfo& withHiddenEnumValues(std::vector<uint8_t> values) {
    hiddenEnumValues = std::move(values);
    return *this;
  }

  static SettingInfo Toggle(StrId nameId, uint8_t CrossPointSettings::* ptr, const char* key = nullptr,
                            StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::TOGGLE;
    s.valuePtr = ptr;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo Enum(StrId nameId, uint8_t CrossPointSettings::* ptr, std::vector<StrId> values,
                          const char* key = nullptr, StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ENUM;
    s.valuePtr = ptr;
    s.enumValues = std::move(values);
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo Header(StrId nameId) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ACTION;
    s.action = SettingAction::None;
    s.isHeader = true;
    return s;
  }

  static SettingInfo Action(StrId nameId, SettingAction action) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ACTION;
    s.action = action;
    return s;
  }

  static SettingInfo Value(StrId nameId, uint8_t CrossPointSettings::* ptr, const ValueRange valueRange,
                           const char* key = nullptr, StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::VALUE;
    s.valuePtr = ptr;
    s.valueRange = valueRange;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo String(StrId nameId, char* ptr, size_t maxLen, const char* key = nullptr,
                            StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::STRING;
    s.stringOffset = (size_t)ptr - (size_t)&SETTINGS;
    s.stringMaxLen = maxLen;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo DynamicEnum(StrId nameId, std::vector<StrId> values, std::function<uint8_t()> getter,
                                 std::function<void(uint8_t)> setter, const char* key = nullptr,
                                 StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ENUM;
    s.enumValues = std::move(values);
    s.valueGetter = std::move(getter);
    s.valueSetter = std::move(setter);
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo DynamicString(StrId nameId, std::function<std::string()> getter,
                                   std::function<void(const std::string&)> setter, const char* key = nullptr,
                                   StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::STRING;
    s.stringGetter = std::move(getter);
    s.stringSetter = std::move(setter);
    s.key = key;
    s.category = category;
    return s;
  }
};

class SettingsActivity final : public Activity {
  ButtonNavigator buttonNavigator;

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
  // isHeader per cell. Always false now that headings are dropped when a category is
  // taken; kept because the settings list still marks them and a future span-aware grid
  // would want them.
  std::vector<bool> headerFlags;

  bool preserveQuickResumeTimeoutOn = false;
  bool quickResumeTimeoutAutoEnabled = false;

  OptionPopup optionPopup;
  // Numeric rows open a slider instead of stepping by one per press. Stepping meant up to
  // 57 presses to cross the Reading Idle Limit range, and wrapping past the maximum to get
  // back. Same component TextSettingsActivity already uses for the margin rows.
  ValueBarPopup valueBar;

  // First grid row drawn, owned here so it survives between frames. Reset whenever the
  // grid underneath it changes identity (a different category, or re-entry).
  int scrollRow = 0;

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
  // The active category's rows, with its group headings dropped: a cell names itself, and
  // a heading band would cost a whole grid row to repeat what the order already says.
  void selectCategory(int index);
  std::vector<SettingInfo>& categoryRows(int index);
  StrId categoryName(int index) const;
  std::string settingValueText(const SettingInfo& setting) const;
  settings_grid::Layout gridLayout() const;
  Rect gridPane() const;
  void drawCell(const settings_grid::Rect& rect, const std::string& name, const std::string& value, bool selected);
  void moveSelection(int deltaRows, int deltaCells);
  void syncQuickResumeTimeoutForSleepScreen(bool sleepScreenChanged, bool quickResumeTimeoutChanged);

 public:
  explicit SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Settings", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
