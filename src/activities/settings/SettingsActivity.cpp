#include "SettingsActivity.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "ButtonRemapActivity.h"
#include "CleanStorageActivity.h"
#include "ClearCacheActivity.h"
#include "CrossPointSettings.h"
#include "FontDownloadActivity.h"
#include "InstalledFontsActivity.h"
#include "KOReaderSettingsActivity.h"
#include "LanguageSelectActivity.h"
#include "MappedInputManager.h"
#include "OpdsServerListActivity.h"
#include "OtaUpdateActivity.h"
#include "PopupItemsActivity.h"
#include "SdCardFontSystem.h"
#include "SdFirmwareUpdateActivity.h"
#include "SettingsList.h"
#include "StatusBarSettingsActivity.h"
#include "TextSettingsActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/BusyBanner.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "sleep/SleepWallpaperIndexStore.h"

const StrId SettingsActivity::categoryNames[categoryCount] = {StrId::STR_CAT_DISPLAY, StrId::STR_CAT_READER,
                                                              StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM};

namespace {

// One section of a tab: the heading, then the rows under it in display order.
struct SettingsGroup {
  StrId heading;
  std::vector<StrId> members;
};

// Reorders a tab's rows into the given sections and inserts a heading above each.
// The group map, not SettingsList.h's declaration order, decides what the user sees.
//
// A group whose rows are all absent contributes no heading, which is what keeps the
// headings honest when a conditional row drops out (Quick-return from footnotes, Pop-up
// Items, Screen Cleanup on X4) or when a whole group is board-specific.
//
// Rows the map does not mention keep their relative order and are appended at the end
// under no heading. That is deliberate: a setting added to SettingsList.h later stays
// reachable on the device even if whoever added it never touched this map.
void applyGroups(std::vector<SettingInfo>& rows, const std::vector<SettingsGroup>& groups) {
  std::vector<SettingInfo> grouped;
  grouped.reserve(rows.size() + groups.size());
  std::vector<bool> placed(rows.size(), false);

  for (const auto& group : groups) {
    bool headingDrawn = false;
    for (const StrId member : group.members) {
      for (size_t i = 0; i < rows.size(); ++i) {
        if (placed[i] || rows[i].nameId != member) continue;
        if (!headingDrawn) {
          grouped.push_back(SettingInfo::Header(group.heading));
          headingDrawn = true;
        }
        grouped.push_back(rows[i]);
        placed[i] = true;
        break;
      }
    }
  }

  for (size_t i = 0; i < rows.size(); ++i) {
    if (!placed[i]) grouped.push_back(rows[i]);
  }

  rows.swap(grouped);
}

}  // namespace

void SettingsActivity::rebuildSettingsLists() {
  displaySettings.clear();
  readerSettings.clear();
  controlsSettings.clear();
  systemSettings.clear();

  // Pick up any fonts uploaded/deleted over the web server since the last
  // reader activity ran — otherwise the font-family picker shows stale list.
  sdFontSystem.refreshIfDirty();

  // Rescan /dictionaries on every rebuild: cheap (one directory listing) and
  // picks up dictionaries copied to the SD card since the last visit.
  std::vector<DictionaryEntry> dictionaries;
  DictionaryRegistry::discover(dictionaries);

  for (auto& setting : getSettingsList(&sdFontSystem.registry(), &dictionaries)) {
    if (setting.category == StrId::STR_NONE_OPT) continue;
    if (setting.category == StrId::STR_CAT_DISPLAY) {
      displaySettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_READER) {
      // Settings merged into "Text Settings"
      // (they stay in the shared list for the web settings API)
      if (setting.inTextSettings) continue;
      readerSettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_CONTROLS) {
      if (setting.valuePtr == &CrossPointSettings::pwrBtnFootnoteBack &&
          SETTINGS.shortPwrBtn != CrossPointSettings::SHORT_PWRBTN::FOOTNOTES) {
        continue;
      }
      controlsSettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_SYSTEM) {
      systemSettings.push_back(setting);
    }
  }

  // Append ACTION items
  controlsSettings.insert(controlsSettings.begin(),
                          SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons));
  // Pop-up Items only exists to serve a binding set to Menu Pop-up, so it is offered
  // only while at least one of the three bindings actually opens one.
  if (SETTINGS.doubleClickPowerFunction == CrossPointSettings::LP_MENU_POPUP ||
      SETTINGS.longPressMenuFunction == CrossPointSettings::LP_MENU_POPUP ||
      SETTINGS.menuHoldFunction == CrossPointSettings::LP_MENU_POPUP) {
    controlsSettings.push_back(SettingInfo::Action(StrId::STR_POPUP_ITEMS, SettingAction::PopupItems));
  }
  systemSettings.push_back(SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_OPDS_SERVERS, SettingAction::OPDSBrowser));
  displaySettings.push_back(SettingInfo::Action(StrId::STR_SHUFFLE_WALLPAPERS, SettingAction::ShuffleWallpapers));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_CLEAN_STORAGE, SettingAction::CleanStorage));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_SD_FIRMWARE_UPDATE, SettingAction::SdFirmwareUpdate));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_LANGUAGE, SettingAction::Language));
  readerSettings.insert(readerSettings.begin(),
                        SettingInfo::Action(StrId::STR_TEXT_SETTINGS, SettingAction::TextSettings));
  readerSettings.insert(readerSettings.begin() + 1,
                        SettingInfo::Action(StrId::STR_MANAGE_FONTS, SettingAction::DownloadFonts));
  readerSettings.insert(readerSettings.begin() + 2,
                        SettingInfo::Action(StrId::STR_INSTALLED_FONTS, SettingAction::InstalledFonts));
  readerSettings.push_back(SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar));

  // Section headings. Applied last so the ACTION rows spliced in above are grouped
  // alongside the settings they belong with rather than stranded at the ends.
  applyGroups(displaySettings,
              {
                  {StrId::STR_GRP_SLEEP_SCREEN,
                   {StrId::STR_SLEEP_SCREEN, StrId::STR_QUICK_RESUME_TIMEOUT, StrId::STR_WAKE_STRAIGHT_TO_BOOK,
                    StrId::STR_SLEEP_FOOTER_TEXT}},
                  {StrId::STR_GRP_WALLPAPER,
                   {StrId::STR_SLEEP_COVER_MODE, StrId::STR_SLEEP_COVER_FILTER, StrId::STR_SLEEP_IMAGE_QUALITY,
                    StrId::STR_SHOW_SLEEP_IMAGE_FILENAME, StrId::STR_SHOW_SLEEP_FAVORITE_BADGE,
                    StrId::STR_SHOW_SLEEP_WALLPAPER_POSITION, StrId::STR_SHUFFLE_WALLPAPERS}},
                  {StrId::STR_GRP_SCREEN, {StrId::STR_REFRESH_FREQ, StrId::STR_SUNLIGHT_FADING_FIX}},
                  {StrId::STR_GRP_HOME, {StrId::STR_AUTHOR_DISPLAY}},
              });

  applyGroups(readerSettings,
              {
                  {StrId::STR_GRP_TEXT,
                   {StrId::STR_TEXT_SETTINGS, StrId::STR_MANAGE_FONTS, StrId::STR_INSTALLED_FONTS,
                    StrId::STR_DICTIONARY}},
                  {StrId::STR_GRP_PAGE,
                   {StrId::STR_ORIENTATION, StrId::STR_PARAGRAPH_NUMBERS, StrId::STR_PARAGRAPH_NUMBER_SIZE}},
                  {StrId::STR_GRP_LOOK,
                   {StrId::STR_PAPERBACK_LOOK, StrId::STR_PAPERBACK_STATUS, StrId::STR_NIGHT_MODE,
                    StrId::STR_CUSTOMISE_STATUS_BAR}},
              });

  applyGroups(
      controlsSettings,
      {
          {StrId::STR_GRP_BUTTONS,
           {StrId::STR_REMAP_FRONT_BUTTONS, StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION, StrId::STR_SIDE_BTN_LAYOUT}},
          {StrId::STR_GRP_POWER_BUTTON,
           {StrId::STR_SHORT_PWR_BTN, StrId::STR_PWR_BTN_FOOTNOTE_BACK, StrId::STR_DOUBLE_CLICK_POWER}},
          // Pop-up Items sits with the bindings, because it only configures what the
          // pop-up those bindings open actually contains.
          {StrId::STR_GRP_HOLD,
           {StrId::STR_LONG_PRESS_MENU, StrId::STR_MENU_HOLD, StrId::STR_POPUP_ITEMS}},
          {StrId::STR_GRP_BACK, {StrId::STR_BACK_SHORT_TO_FILE_BROWSER, StrId::STR_HOME_BACK_ACTION}},
      });

  applyGroups(
      systemSettings,
      {
          {StrId::STR_GRP_POWER, {StrId::STR_TIME_TO_SLEEP}},
          {StrId::STR_GRP_LIBRARY,
           {StrId::STR_SHOW_HIDDEN_FILES, StrId::STR_BOOK_BROWSER_ORDER, StrId::STR_OPEN_BOOK_ON_BOOT,
            StrId::STR_REMOVE_READ_FROM_RECENTS, StrId::STR_MOVE_FINISHED_TO_READ, StrId::STR_MOVE_OPENED_TO_RECENTS}},
          {StrId::STR_GRP_STATS, {StrId::STR_TRACK_READING_STATS, StrId::STR_READING_IDLE_LIMIT}},
          {StrId::STR_GRP_NETWORK, {StrId::STR_WIFI_NETWORKS, StrId::STR_KOREADER_SYNC, StrId::STR_OPDS_SERVERS}},
          {StrId::STR_GRP_DEVICE,
           {StrId::STR_LANGUAGE, StrId::STR_CLEAN_STORAGE, StrId::STR_CLEAR_READING_CACHE, StrId::STR_CHECK_UPDATES,
            StrId::STR_SD_FIRMWARE_UPDATE}},
      });

  // Update currentSettings pointer and count for the active category
  switch (selectedCategoryIndex) {
    case 0:
      currentSettings = &displaySettings;
      break;
    case 1:
      currentSettings = &readerSettings;
      break;
    case 2:
      currentSettings = &controlsSettings;
      break;
    case 3:
      currentSettings = &systemSettings;
      break;
  }
  settingsCount = static_cast<int>(currentSettings->size());
}

void SettingsActivity::onEnter() {
  Activity::onEnter();

  // Opening Settings rescans SD fonts and dictionaries before anything is drawn,
  // so on a loaded card this is a silent wait on the previous screen. Armed only
  // here, not around the rebuilds triggered by changing a setting: those must stay
  // instant, and flashing a banner on every toggle would be worse than nothing.
  BusyBanner banner(renderer, tr(STR_BUSY_LOADING_SETTINGS));

  // Reset selection to first category
  selectedCategoryIndex = 0;
  selectedSettingIndex = 0;
  preserveQuickResumeTimeoutOn =
      SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
  quickResumeTimeoutAutoEnabled = false;
  syncQuickResumeTimeoutForSleepScreen(/*sleepScreenChanged=*/true, /*quickResumeTimeoutChanged=*/false);

  rebuildSettingsLists();

  // Trigger first update
  requestUpdate();
}

void SettingsActivity::onExit() {
  Activity::onExit();

  UITheme::getInstance().reload();  // Re-apply theme in case it was changed
}

void SettingsActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
  if (valueBar.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  bool hasChangedCategory = false;

  auto applyCategorySelection = [this] {
    switch (selectedCategoryIndex) {
      case 0:
        currentSettings = &displaySettings;
        break;
      case 1:
        currentSettings = &readerSettings;
        break;
      case 2:
        currentSettings = &controlsSettings;
        break;
      case 3:
        currentSettings = &systemSettings;
        break;
    }
    settingsCount = static_cast<int>(currentSettings->size());
  };

  // Handle actions with early return
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedSettingIndex == 0) {
      selectedCategoryIndex = (selectedCategoryIndex < categoryCount - 1) ? (selectedCategoryIndex + 1) : 0;
      hasChangedCategory = true;
      requestUpdate();
    } else {
      toggleCurrentSetting();
      requestUpdate();
      return;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (selectedSettingIndex > 0) {
      selectedSettingIndex = 0;
      requestUpdate();
    } else {
      SETTINGS.saveToFile();
      onGoHome();
    }
    return;
  }

  buttonNavigator.onNextStep([this] {
    selectedSettingIndex = skipHeaders(ButtonNavigator::nextIndex(selectedSettingIndex, settingsCount + 1), true);
    requestUpdate();
  });

  buttonNavigator.onPreviousStep([this] {
    selectedSettingIndex = skipHeaders(ButtonNavigator::previousIndex(selectedSettingIndex, settingsCount + 1), false);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, &hasChangedCategory] {
    hasChangedCategory = true;
    selectedCategoryIndex = ButtonNavigator::nextIndex(selectedCategoryIndex, categoryCount);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, &hasChangedCategory] {
    hasChangedCategory = true;
    selectedCategoryIndex = ButtonNavigator::previousIndex(selectedCategoryIndex, categoryCount);
    requestUpdate();
  });

  if (hasChangedCategory) {
    selectedSettingIndex = (selectedSettingIndex == 0) ? 0 : 1;
    applyCategorySelection();
    // Every tab now opens on a heading, so the first row has to be stepped past.
    // The old tab's scroll position means nothing in the new one.
    selectedSettingIndex = skipHeaders(selectedSettingIndex, true);
    listScrollOffset = 0;
  }
}

bool SettingsActivity::isHeaderRow(const int navIndex) const {
  const int row = navIndex - 1;
  if (currentSettings == nullptr || row < 0 || row >= settingsCount) return false;
  return (*currentSettings)[row].isHeader;
}

int SettingsActivity::skipHeaders(int navIndex, const bool forward) const {
  const int ring = settingsCount + 1;
  // Bounded by the ring length: a list that somehow held nothing but headings would
  // otherwise spin here forever.
  for (int guard = 0; guard < ring && isHeaderRow(navIndex); ++guard) {
    navIndex = forward ? ButtonNavigator::nextIndex(navIndex, ring) : ButtonNavigator::previousIndex(navIndex, ring);
  }
  return navIndex;
}

void SettingsActivity::toggleCurrentSetting() {
  int selectedSetting = selectedSettingIndex - 1;
  if (selectedSetting < 0 || selectedSetting >= settingsCount) {
    return;
  }
  // Confirm on a heading does nothing; it is a divider, not an option.
  if ((*currentSettings)[selectedSetting].isHeader) {
    return;
  }

  const auto& setting = (*currentSettings)[selectedSetting];
  const bool sleepScreenChanged = setting.valuePtr == &CrossPointSettings::sleepScreen;
  const bool quickResumeTimeoutChanged = setting.valuePtr == &CrossPointSettings::quickResumeSleepScreen;

  if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
    openSleepTimeoutPicker();
    return;
  }

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    // Toggle the boolean value using the member pointer
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    if (setting.enumValues.size() > 2) {
      const auto valuePtr = setting.valuePtr;
      // Retired options keep their enum slot but leave the picker, so the popup runs on a
      // filtered list and offeredValues maps a popup row back to the value it stands for.
      std::vector<StrId> offeredLabels;
      std::vector<uint8_t> offeredValues;
      offeredLabels.reserve(setting.enumValues.size());
      offeredValues.reserve(setting.enumValues.size());
      int currentRow = 0;
      for (uint8_t value = 0; value < static_cast<uint8_t>(setting.enumValues.size()); value++) {
        if (setting.isEnumValueHidden(value)) continue;
        if (value == currentValue) currentRow = static_cast<int>(offeredValues.size());
        offeredLabels.push_back(setting.enumValues[value]);
        offeredValues.push_back(value);
      }
      optionPopup.show(setting.nameId, offeredLabels.data(), static_cast<int>(offeredLabels.size()), currentRow,
                       [this, valuePtr, offeredValues, sleepScreenChanged, quickResumeTimeoutChanged](int idx) {
                         if (idx < 0 || idx >= static_cast<int>(offeredValues.size())) return;
                         SETTINGS.*valuePtr = offeredValues[idx];
                         syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
                         SETTINGS.saveToFile();
                         rebuildSettingsLists();
                       });
      requestUpdate();
      return;
    }
    // Two-value settings cycle in place. The loop steps over hidden values for the same
    // reason the picker skips them; it is bounded by the value count, so an enum whose
    // every value is hidden simply stays where it is.
    const auto valueCount = static_cast<uint8_t>(setting.enumValues.size());
    uint8_t nextValue = currentValue;
    for (uint8_t step = 0; step < valueCount; step++) {
      nextValue = (nextValue + 1) % valueCount;
      if (!setting.isEnumValueHidden(nextValue)) break;
    }
    SETTINGS.*(setting.valuePtr) = nextValue;
  } else if (setting.type == SettingType::ENUM && setting.valueGetter && setting.valueSetter) {
    const uint8_t totalValues = setting.enumStringValues.empty()
                                    ? static_cast<uint8_t>(setting.enumValues.size())
                                    : static_cast<uint8_t>(setting.enumStringValues.size());
    const uint8_t cur = setting.valueGetter();
    if (totalValues > 2) {
      const auto valueSetter = setting.valueSetter;
      auto onSelect = [this, valueSetter, sleepScreenChanged, quickResumeTimeoutChanged](int idx) {
        valueSetter(idx);
        syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
        SETTINGS.saveToFile();
        rebuildSettingsLists();
      };
      if (!setting.enumStringValues.empty()) {
        optionPopup.show(setting.nameId, setting.enumStringValues, cur, std::move(onSelect));
      } else {
        optionPopup.show(setting.nameId, setting.enumValues.data(), static_cast<int>(setting.enumValues.size()), cur,
                         std::move(onSelect));
      }
      requestUpdate();
      return;
    }
    setting.valueSetter((cur + 1) % totalValues);
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    // Front Left/Right step by the setting's own step; the side buttons jump by five of
    // them, so a wide range is crossed in a few presses rather than tapped across one at
    // a time. Back cancels, which stepping in place could not offer.
    const auto valuePtr = setting.valuePtr;
    valueBar.show(setting.nameId, setting.valueRange.min, setting.valueRange.max, setting.valueRange.step,
                  setting.valueRange.step * 5, SETTINGS.*(setting.valuePtr), setting.nameId,
                  [this, valuePtr](const int chosen) {
                    SETTINGS.*valuePtr = static_cast<uint8_t>(chosen);
                    SETTINGS.saveToFile();
                    rebuildSettingsLists();
                  });
    requestUpdate();
    return;
  } else if (setting.type == SettingType::STRING) {
    // Free-text entry via the on-screen keyboard. Handles both direct char[] fields
    // (stringOffset into SETTINGS) and dynamic getter/setter string settings.
    std::string initial;
    if (setting.stringGetter) {
      initial = setting.stringGetter();
    } else if (setting.stringMaxLen > 0) {
      initial = reinterpret_cast<const char*>(reinterpret_cast<const uint8_t*>(&SETTINGS) + setting.stringOffset);
    }
    const size_t offset = setting.stringOffset;
    const size_t maxLen = setting.stringMaxLen;
    const auto stringSetter = setting.stringSetter;
    const size_t maxChars = maxLen > 0 ? maxLen - 1 : 63;
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, std::string(I18N.get(setting.nameId)), initial,
                                                maxChars, InputType::Text),
        [this, offset, maxLen, stringSetter](const ActivityResult& result) {
          if (!result.isCancelled) {
            const auto& kb = std::get<KeyboardResult>(result.data);
            if (stringSetter) {
              stringSetter(kb.text);
            } else if (maxLen > 0) {
              char* dst = reinterpret_cast<char*>(reinterpret_cast<uint8_t*>(&SETTINGS) + offset);
              strncpy(dst, kb.text.c_str(), maxLen - 1);
              dst[maxLen - 1] = '\0';
            }
            SETTINGS.saveToFile();
            rebuildSettingsLists();
          }
        });
    return;
  } else if (setting.type == SettingType::ACTION) {
    auto resultHandler = [this](const ActivityResult&) { SETTINGS.saveToFile(); };

    switch (setting.action) {
      case SettingAction::RemapFrontButtons:
        startActivityForResult(std::make_unique<ButtonRemapActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CustomiseStatusBar:
        startActivityForResult(std::make_unique<StatusBarSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::PopupItems:
        startActivityForResult(std::make_unique<PopupItemsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::KOReaderSync:
        startActivityForResult(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::OPDSBrowser:
        startActivityForResult(std::make_unique<OpdsServerListActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Network:
        startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, false), resultHandler);
        break;
      case SettingAction::ShuffleWallpapers: {
        // Inline, no sub-activity: the reshuffle is a reseed of the rotation
        // cursor plus one state.json save — the index file is untouched.
        GUI.drawPopup(renderer, tr(STR_SHUFFLING_WALLPAPERS));
        const bool shuffled = crosspoint::sleep::windex::reshuffleNow();
        GUI.drawPopup(renderer, shuffled ? tr(STR_WALLPAPERS_SHUFFLED) : tr(STR_SHUFFLE_EMPTY));
        break;
      }
      case SettingAction::CleanStorage:
        startActivityForResult(std::make_unique<CleanStorageActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::ClearCache:
        startActivityForResult(std::make_unique<ClearCacheActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CheckForUpdates:
        startActivityForResult(std::make_unique<OtaUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::SdFirmwareUpdate:
        startActivityForResult(std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::DownloadFonts:
        startActivityForResult(std::make_unique<FontDownloadActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                 SETTINGS.saveToFile();
                                 rebuildSettingsLists();
                               });
        break;
      case SettingAction::InstalledFonts:
        startActivityForResult(std::make_unique<InstalledFontsActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                 SETTINGS.saveToFile();
                                 rebuildSettingsLists();
                               });
        break;
      case SettingAction::TextSettings:
        startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                                      TextSettingsActivity::Tab::Family),
                               [this](const ActivityResult&) {
                                 // TextSettingsActivity saves on each change; no save needed here.
                                 rebuildSettingsLists();
                               });
        break;
      case SettingAction::Language:
        // Row labels are translated once in rebuildRowItems() and don't
        // re-run on Pop (see ActivityManager::loop()), so a language switch
        // needs an explicit rebuild here rather than the generic resultHandler.
        startActivityForResult(std::make_unique<LanguageSelectActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                 SETTINGS.saveToFile();
                                 rebuildSettingsLists();
                               });
        break;
      case SettingAction::None:
        // Do nothing
        break;
    }
    return;  // Results will be handled in the result handler, so we can return early here
  } else {
    return;
  }

  syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
  SETTINGS.saveToFile();
  rebuildSettingsLists();
  selectedSettingIndex = std::min(selectedSettingIndex, settingsCount);
  // A toggle can add or remove rows (Quick-return from footnotes, Pop-up Items), which
  // shifts everything below it and can slide a heading under the cursor.
  selectedSettingIndex = skipHeaders(selectedSettingIndex, true);
}

void SettingsActivity::syncQuickResumeTimeoutForSleepScreen(bool sleepScreenChanged, bool quickResumeTimeoutChanged) {
  if (quickResumeTimeoutChanged) {
    preserveQuickResumeTimeoutOn =
        SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
    quickResumeTimeoutAutoEnabled = false;
  }

  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME) {
    if (SETTINGS.quickResumeSleepScreen != CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT) {
      SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
      quickResumeTimeoutAutoEnabled = !preserveQuickResumeTimeoutOn;
    } else if (sleepScreenChanged && !preserveQuickResumeTimeoutOn) {
      quickResumeTimeoutAutoEnabled = true;
    }
    return;
  }

  if (sleepScreenChanged && quickResumeTimeoutAutoEnabled && !preserveQuickResumeTimeoutOn) {
    SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_NEVER;
    quickResumeTimeoutAutoEnabled = false;
  }
}

void SettingsActivity::openSleepTimeoutPicker() {
  startActivityForResult(
      std::make_unique<IntervalSelectionActivity>(
          renderer, mappedInput, "SleepTimeoutInterval", StrId::STR_TIME_TO_SLEEP, SETTINGS.sleepTimeoutMinutes,
          CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1, 5,
          StrId::STR_SLEEP_TIMER_VALUE_FORMAT, false, true, StrId::STR_SLEEP_NEVER),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          SETTINGS.sleepTimeoutMinutes = static_cast<uint8_t>(std::get<IntervalResult>(result.data).value);
          SETTINGS.saveToFile();
        }
        requestUpdate();
      });
}

void SettingsActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  if (valueBar.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SETTINGS_TITLE),
                 CROSSPOINT_VERSION);

  std::vector<TabInfo> tabs;
  tabs.reserve(categoryCount);
  for (int i = 0; i < categoryCount; i++) {
    tabs.push_back({I18N.get(categoryNames[i]), selectedCategoryIndex == i});
  }
  GUI.drawTabBar(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight}, tabs,
                 selectedSettingIndex == 0);

  const auto& settings = *currentSettings;
  GUI.drawList(
      renderer,
      Rect{0, metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing, pageWidth,
           pageHeight - (metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.buttonHintsHeight +
                         metrics.verticalSpacing * 2)},
      settingsCount, selectedSettingIndex - 1,
      [&settings](int index) { return std::string(I18N.get(settings[index].nameId)); }, nullptr, nullptr,
      [&settings](int i) {
        const auto& setting = settings[i];
        std::string valueText = "";
        if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
          const bool value = SETTINGS.*(setting.valuePtr);
          valueText = value ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
        } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
          const uint8_t value = SETTINGS.*(setting.valuePtr);
          valueText = I18N.get(setting.enumValues[value]);
        } else if (setting.type == SettingType::ENUM && setting.valueGetter) {
          const uint8_t value = setting.valueGetter();
          if (!setting.enumStringValues.empty() && value < setting.enumStringValues.size()) {
            valueText = setting.enumStringValues[value];
          } else if (value < setting.enumValues.size()) {
            valueText = I18N.get(setting.enumValues[value]);
          }
        } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
          if (setting.nameId == StrId::STR_READING_IDLE_LIMIT) {
            // Stored in 10-second units, which means nothing on screen. Whole minutes
            // read as minutes; the steps between them read as seconds.
            char valueBuffer[32];
            const unsigned seconds = SETTINGS.readingStatsIdleSeconds();
            if (seconds % 60 == 0) {
              snprintf(valueBuffer, sizeof(valueBuffer), tr(STR_SLEEP_TIMER_VALUE_FORMAT), seconds / 60);
            } else {
              snprintf(valueBuffer, sizeof(valueBuffer), tr(STR_SECONDS_VALUE_FORMAT), seconds);
            }
            valueText = valueBuffer;
          } else if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
            char valueBuffer[32];
            if (SETTINGS.sleepTimeoutMinutes >= CrossPointSettings::SLEEP_TIMEOUT_NEVER_MINUTES) {
              valueText = tr(STR_SLEEP_NEVER);
            } else {
              snprintf(valueBuffer, sizeof(valueBuffer), tr(STR_SLEEP_TIMER_VALUE_FORMAT),
                       static_cast<unsigned int>(SETTINGS.*(setting.valuePtr)));
              valueText = valueBuffer;
            }
          } else {
            valueText = std::to_string(SETTINGS.*(setting.valuePtr));
          }
        } else if (setting.type == SettingType::STRING) {
          if (setting.stringGetter) {
            valueText = setting.stringGetter();
          } else if (setting.stringMaxLen > 0) {
            valueText =
                reinterpret_cast<const char*>(reinterpret_cast<const uint8_t*>(&SETTINGS) + setting.stringOffset);
          }
        }
        return valueText;
      },
      true, nullptr, UI_10_FONT_ID, [&settings](int i) { return settings[i].isHeader; }, &listScrollOffset);

  // Draw help text
  const auto confirmLabel =
      (selectedSettingIndex == 0)
          ? I18N.get(categoryNames[(selectedCategoryIndex + 1) % categoryCount])
          : (selectedSettingIndex > 0 && (*currentSettings)[selectedSettingIndex - 1].nameId == StrId::STR_TIME_TO_SLEEP
                 ? tr(STR_SELECT)
                 : tr(STR_TOGGLE));

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
}
