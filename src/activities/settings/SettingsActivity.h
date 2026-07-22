#pragma once
#include <I18n.h>

#include <functional>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/Activity.h"
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
  CheckForUpdates,
  SdFirmwareUpdate,
  Language,
  RandomizeSleepImages,
  MoveNonFavoritesToPause,
  MoveFavoritesToPause,
  MoveFavoritesToSleep,
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
  bool obfuscated = false;               // Save/load via base64 obfuscation (passwords)

  // Direct char[] string fields (for settings stored in CrossPointSettings)
  size_t stringOffset = 0;
  size_t stringMaxLen = 0;

  // Dynamic accessors (for settings stored outside CrossPointSettings, e.g.
  // KOReaderCredentialStore, or the registry-aware font-family entry). Plain
  // function pointers + one ctx, not std::function: the four std::function slots
  // cost ~128B on every one of the ~76 entries even though only ~7 use them, and
  // heap-allocated the font entry's captured family-list copy each call. Now zero
  // heap closures and ~7KB off the per-call list. dynCtx carries the SD font
  // registry to the font-family getter/setter so it stays registry-dependent
  // exactly as before (nullptr => built-in only); nullptr for every other entry.
  uint8_t (*valueGetter)(const void* ctx) = nullptr;
  void (*valueSetter)(const void* ctx, uint8_t) = nullptr;
  std::string (*stringGetter)(const void* ctx) = nullptr;
  void (*stringSetter)(const void* ctx, const std::string&) = nullptr;
  const void* dynCtx = nullptr;

  SettingInfo& withObfuscated() {
    obfuscated = true;
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

  static SettingInfo DynamicEnum(StrId nameId, std::vector<StrId> values, uint8_t (*getter)(const void*),
                                 void (*setter)(const void*, uint8_t), const char* key = nullptr,
                                 StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ENUM;
    s.enumValues = std::move(values);
    s.valueGetter = getter;
    s.valueSetter = setter;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo DynamicString(StrId nameId, std::string (*getter)(const void*),
                                   void (*setter)(const void*, const std::string&), const char* key = nullptr,
                                   StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::STRING;
    s.stringGetter = getter;
    s.stringSetter = setter;
    s.key = key;
    s.category = category;
    return s;
  }
};

class SettingsActivity final : public Activity {
  ButtonNavigator buttonNavigator;

  int selectedCategoryIndex = 0;  // Currently selected category
  int selectedSettingIndex = 0;
  int settingsCount = 0;

  // Per-category settings derived from shared list + device-only actions
  std::vector<SettingInfo> displaySettings;
  std::vector<SettingInfo> readerSettings;
  std::vector<SettingInfo> controlsSettings;
  std::vector<SettingInfo> systemSettings;
  const std::vector<SettingInfo>* currentSettings = nullptr;

  bool preserveQuickResumeTimeoutOn = false;
  bool quickResumeTimeoutAutoEnabled = false;

  static constexpr int categoryCount = 4;
  static const StrId categoryNames[categoryCount];

  void enterCategory(int categoryIndex);
  void toggleCurrentSetting();
  void openSleepTimeoutPicker();
  void openLineSpacingPicker();
  void openFirstLineIndentPicker();
  void openWordSpacingPicker();
  void openParagraphSpacingPicker();
  void openMarginPicker(uint8_t CrossPointSettings::* field, StrId titleId);
  void rebuildSettingsLists();
  void syncQuickResumeTimeoutForSleepScreen(bool sleepScreenChanged, bool quickResumeTimeoutChanged);

  // When >= 0, the screen is locked to a single category (used by the in-book
  // "Reader" tab, which opens this screen locked to the Reader category to edit a
  // book's per-book reader settings). Category switching is disabled and Back
  // returns to the caller via finish() instead of going home. -1 = normal
  // top-level Settings (all four tabs, Back goes home).
  int lockedCategory_ = -1;

 public:
  explicit SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int lockedCategory = -1)
      : Activity("Settings", renderer, mappedInput), lockedCategory_(lockedCategory) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
