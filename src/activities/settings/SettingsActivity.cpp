#include "SettingsActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "ButtonRemapActivity.h"
#include "ClearCacheActivity.h"
#include "CrossPointSettings.h"
#include "FontSelectionActivity.h"
#include "KOReaderSettingsActivity.h"
#include "LanguageSelectActivity.h"
#include "MappedInputManager.h"
#include "OpdsServerListActivity.h"
#include "OtaUpdateActivity.h"
#include "SdCardFontSystem.h"
#include "SdFirmwareUpdateActivity.h"
#include "SettingSelectActivity.h"
#include "SettingsList.h"
#include "StatusBarSettingsActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "sleep/Wallpaper.h"

const StrId SettingsActivity::categoryNames[categoryCount] = {StrId::STR_CAT_DISPLAY, StrId::STR_CAT_READER,
                                                              StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM};

void SettingsActivity::rebuildSettingsLists() {
  displaySettings.clear();
  readerSettings.clear();
  controlsSettings.clear();
  systemSettings.clear();

  // Pick up any fonts uploaded/deleted over the web server since the last
  // reader activity ran — otherwise the font-family picker shows stale list.
  sdFontSystem.refreshIfDirty();

  for (auto& setting : getSettingsList(&sdFontSystem.registry())) {
    if (setting.category == StrId::STR_NONE_OPT) continue;
    if (setting.category == StrId::STR_CAT_DISPLAY) {
      displaySettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_READER) {
      // Top/Bottom margins only apply, and only show, when uniform margins are off.
      if ((setting.nameId == StrId::STR_SCREEN_MARGIN_TOP || setting.nameId == StrId::STR_SCREEN_MARGIN_BOTTOM) &&
          SETTINGS.uniformMargins) {
        continue;
      }
      // The first-line indent percentage only shows in custom (non-book) mode.
      if (setting.nameId == StrId::STR_FIRST_LINE_INDENT_PERCENT &&
          SETTINGS.firstLineIndentMode != CrossPointSettings::FIRST_LINE_INDENT_PERCENT) {
        continue;
      }
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

  // Append device-only ACTION items
  controlsSettings.insert(controlsSettings.begin(),
                          SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_OPDS_SERVERS, SettingAction::OPDSBrowser));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_SD_FIRMWARE_UPDATE, SettingAction::SdFirmwareUpdate));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_LANGUAGE, SettingAction::Language));
  // Sleep-wallpaper "shuffle now" lives in the Display category next to the
  // other sleep-screen settings.
  displaySettings.push_back(
      SettingInfo::Action(StrId::STR_RANDOMIZE_SLEEP_IMAGES, SettingAction::RandomizeSleepImages));
  readerSettings.push_back(SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar));

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
  bool hasChangedCategory = false;

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

  // Handle navigation
  buttonNavigator.onNextPress([this] {
    selectedSettingIndex = ButtonNavigator::nextIndex(selectedSettingIndex, settingsCount + 1);
    requestUpdate();
  });

  buttonNavigator.onPreviousPress([this] {
    selectedSettingIndex = ButtonNavigator::previousIndex(selectedSettingIndex, settingsCount + 1);
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
}

void SettingsActivity::toggleCurrentSetting() {
  int selectedSetting = selectedSettingIndex - 1;
  if (selectedSetting < 0 || selectedSetting >= settingsCount) {
    return;
  }

  const auto& setting = (*currentSettings)[selectedSetting];
  const bool sleepScreenChanged = setting.valuePtr == &CrossPointSettings::sleepScreen;
  const bool quickResumeTimeoutChanged = setting.valuePtr == &CrossPointSettings::quickResumeSleepScreen;

  if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
    openSleepTimeoutPicker();
    return;
  }

  if (setting.nameId == StrId::STR_LINE_SPACING) {
    openLineSpacingPicker();
    return;
  }

  if (setting.nameId == StrId::STR_SCREEN_MARGIN) {
    openMarginPicker(&CrossPointSettings::screenMargin, StrId::STR_SCREEN_MARGIN);
    return;
  }
  if (setting.nameId == StrId::STR_SCREEN_MARGIN_TOP) {
    openMarginPicker(&CrossPointSettings::screenMarginTop, StrId::STR_SCREEN_MARGIN_TOP);
    return;
  }
  if (setting.nameId == StrId::STR_SCREEN_MARGIN_BOTTOM) {
    openMarginPicker(&CrossPointSettings::screenMarginBottom, StrId::STR_SCREEN_MARGIN_BOTTOM);
    return;
  }
  if (setting.nameId == StrId::STR_FIRST_LINE_INDENT_PERCENT) {
    openFirstLineIndentPicker();
    return;
  }
  if (setting.nameId == StrId::STR_WORD_SPACING) {
    openWordSpacingPicker();
    return;
  }
  if (setting.nameId == StrId::STR_PARAGRAPH_SPACING) {
    openParagraphSpacingPicker();
    return;
  }

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    // Toggle the boolean value using the member pointer
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
    // Turning uniform margins on seeds top/bottom from the horizontal value so
    // toggling back off inherits it instead of snapping to a stale value.
    if (setting.valuePtr == &CrossPointSettings::uniformMargins && SETTINGS.uniformMargins) {
      SETTINGS.screenMarginTop = SETTINGS.screenMargin;
      SETTINGS.screenMarginBottom = SETTINGS.screenMargin;
    }
  } else if (setting.type == SettingType::ENUM) {
    // Font family keeps its dedicated preview picker.
    if (setting.nameId == StrId::STR_FONT_FAMILY && setting.valueGetter && setting.valueSetter) {
      startActivityForResult(makeUniqueNoThrow<FontSelectionActivity>(renderer, mappedInput, &sdFontSystem.registry()),
                             [this](const ActivityResult&) {
                               SETTINGS.saveToFile();
                               rebuildSettingsLists();
                             });
      return;
    }

    // Every other multi-option setting opens the scroll-and-select list picker
    // (up/down to move, Confirm to select) instead of tap-to-cycle. Build the
    // labels + current index + a setter, then hand them to SettingSelectActivity.
    std::vector<std::string> options;
    int current = 0;
    std::function<void(uint8_t)> apply;
    if (setting.valuePtr != nullptr) {
      options.reserve(setting.enumValues.size());
      for (const auto id : setting.enumValues) options.emplace_back(I18N.get(id));
      current = SETTINGS.*(setting.valuePtr);
      const auto ptr = setting.valuePtr;
      apply = [ptr](uint8_t idx) { SETTINGS.*ptr = idx; };
    } else if (setting.valueGetter && setting.valueSetter) {
      if (!setting.enumStringValues.empty()) {
        options = setting.enumStringValues;
      } else {
        options.reserve(setting.enumValues.size());
        for (const auto id : setting.enumValues) options.emplace_back(I18N.get(id));
      }
      current = setting.valueGetter();
      const auto setter = setting.valueSetter;
      apply = [setter](uint8_t idx) { setter(idx); };
    } else {
      return;
    }

    startActivityForResult(
        makeUniqueNoThrow<SettingSelectActivity>(renderer, mappedInput, std::string(I18N.get(setting.nameId)),
                                                 std::move(options), current, std::move(apply)),
        [this, sleepScreenChanged, quickResumeTimeoutChanged](const ActivityResult&) {
          syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
          SETTINGS.saveToFile();
          rebuildSettingsLists();
          selectedSettingIndex = std::min(selectedSettingIndex, settingsCount);
        });
    return;
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    // Tap-to-increment (wraps at max) for numeric settings edited in place.
    // Use int, not int8_t, so ranges above 127 stay correct.
    const int currentValue = SETTINGS.*(setting.valuePtr);
    if (currentValue + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = static_cast<uint8_t>(currentValue + setting.valueRange.step);
    }
  } else if (setting.type == SettingType::ACTION) {
    auto resultHandler = [this](const ActivityResult&) { SETTINGS.saveToFile(); };

    switch (setting.action) {
      case SettingAction::RemapFrontButtons:
        startActivityForResult(makeUniqueNoThrow<ButtonRemapActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CustomiseStatusBar:
        startActivityForResult(makeUniqueNoThrow<StatusBarSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::KOReaderSync:
        startActivityForResult(makeUniqueNoThrow<KOReaderSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::OPDSBrowser:
        startActivityForResult(makeUniqueNoThrow<OpdsServerListActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Network:
        startActivityForResult(makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput, false), resultHandler);
        break;
      case SettingAction::ClearCache:
        startActivityForResult(makeUniqueNoThrow<ClearCacheActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CheckForUpdates:
        startActivityForResult(makeUniqueNoThrow<OtaUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::SdFirmwareUpdate:
        startActivityForResult(makeUniqueNoThrow<SdFirmwareUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Language:
        startActivityForResult(makeUniqueNoThrow<LanguageSelectActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::RandomizeSleepImages: {
        // Inline: one-shot reshuffle of the sleep wallpaper rotation, then a
        // confirmation banner. reshuffle() persists the new order itself.
        const bool shuffled = crosspoint::sleep::wallpaper::reshuffle();
        GUI.drawPopup(renderer, shuffled ? tr(STR_SLEEP_SHUFFLED) : tr(STR_SLEEP_SHUFFLE_EMPTY));
        break;
      }
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
      makeUniqueNoThrow<IntervalSelectionActivity>(
          renderer, mappedInput, "SleepTimeoutInterval", StrId::STR_TIME_TO_SLEEP, StrId::STR_SLEEP_TIMER_STEP_HINT,
          SETTINGS.sleepTimeoutMinutes, CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES,
          CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1, 5, StrId::STR_SLEEP_TIMER_VALUE_FORMAT, false, true,
          StrId::STR_SLEEP_NEVER),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          SETTINGS.sleepTimeoutMinutes = static_cast<uint8_t>(std::get<IntervalResult>(result.data).value);
          SETTINGS.saveToFile();
        }
        requestUpdate();
      });
}

void SettingsActivity::openWordSpacingPicker() {
  // Word spacing as a signed percentage of the inter-word space width: -30% (tight)
  // to +300% (loose), 0% = the font's natural spacing. Stored as a 10%-step count.
  startActivityForResult(
      makeUniqueNoThrow<IntervalSelectionActivity>(
          renderer, mappedInput, "WordSpacingInterval", StrId::STR_WORD_SPACING, StrId::STR_SPACING_STEP_HINT,
          SETTINGS.wordSpacingPercent(), CrossPointSettings::MIN_WORD_SPACING_PERCENT,
          CrossPointSettings::MAX_WORD_SPACING_PERCENT, 10, 50, StrId::STR_SPACING_VALUE_FORMAT, false, true),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          SETTINGS.wordSpacing =
              CrossPointSettings::wordSpacingStepFromPercent(std::get<IntervalResult>(result.data).value);
          SETTINGS.saveToFile();
        }
        requestUpdate();
      });
}

void SettingsActivity::openParagraphSpacingPicker() {
  // Paragraph spacing as a percentage of the line height, injected as a vertical
  // gap between blocks: 0% = off, up to 150%. Stored directly as the percentage.
  startActivityForResult(
      makeUniqueNoThrow<IntervalSelectionActivity>(
          renderer, mappedInput, "ParagraphSpacingInterval", StrId::STR_PARAGRAPH_SPACING, StrId::STR_SPACING_STEP_HINT,
          SETTINGS.paragraphSpacing, 0, CrossPointSettings::MAX_PARAGRAPH_SPACING, 10, 50,
          StrId::STR_SPACING_VALUE_FORMAT, false, true),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          SETTINGS.paragraphSpacing = static_cast<uint8_t>(std::get<IntervalResult>(result.data).value);
          SETTINGS.saveToFile();
        }
        requestUpdate();
      });
}

void SettingsActivity::openMarginPicker(uint8_t CrossPointSettings::* field, StrId titleId) {
  // Reader margins are a fine 0..60 range, so a slider (up/down = 1, page = 10)
  // beats tap-to-increment. Shared by the uniform/horizontal, top and bottom
  // margin rows; the caller passes which field to edit and its title.
  startActivityForResult(makeUniqueNoThrow<IntervalSelectionActivity>(
                             renderer, mappedInput, "MarginInterval", titleId, StrId::STR_MARGIN_STEP_HINT,
                             SETTINGS.*field, CrossPointSettings::MIN_SCREEN_MARGIN,
                             CrossPointSettings::MAX_SCREEN_MARGIN, 1, 10, StrId::STR_MARGIN_VALUE_FORMAT, false, true),
                         [this, field](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             SETTINGS.*field = static_cast<uint8_t>(std::get<IntervalResult>(result.data).value);
                             SETTINGS.saveToFile();
                           }
                           requestUpdate();
                         });
}

void SettingsActivity::openFirstLineIndentPicker() {
  // Custom first-line indent as a percentage (0% = flush with the body text,
  // 100% = the first line starts at the horizontal middle of the column).
  // Slider: up/down adjusts by 5%, page jump by 25%.
  startActivityForResult(
      makeUniqueNoThrow<IntervalSelectionActivity>(
          renderer, mappedInput, "FirstLineIndentInterval", StrId::STR_FIRST_LINE_INDENT_PERCENT,
          StrId::STR_INDENT_STEP_HINT, SETTINGS.firstLineIndentPercent, 0,
          CrossPointSettings::MAX_FIRST_LINE_INDENT_PERCENT, 5, 25, StrId::STR_INDENT_VALUE_FORMAT, false, true),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          SETTINGS.firstLineIndentPercent = static_cast<uint8_t>(std::get<IntervalResult>(result.data).value);
          SETTINGS.saveToFile();
        }
        requestUpdate();
      });
}

void SettingsActivity::openLineSpacingPicker() {
  // Reader line spacing is a percentage (100% = the font's natural spacing).
  // A slider (like the sleep timer) is nicer than tap-to-increment for a wide,
  // bidirectional range: up/down adjusts by 5%, page jump by 25%.
  startActivityForResult(
      makeUniqueNoThrow<IntervalSelectionActivity>(
          renderer, mappedInput, "LineSpacingInterval", StrId::STR_LINE_SPACING, StrId::STR_LINE_SPACING_STEP_HINT,
          SETTINGS.lineSpacingPercent, CrossPointSettings::MIN_LINE_SPACING_PERCENT,
          CrossPointSettings::MAX_LINE_SPACING_PERCENT, 5, 25, StrId::STR_LINE_SPACING_VALUE_FORMAT, false, true),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          SETTINGS.lineSpacingPercent = static_cast<uint8_t>(std::get<IntervalResult>(result.data).value);
          SETTINGS.saveToFile();
        }
        requestUpdate();
      });
}

void SettingsActivity::render(RenderLock&&) {
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
          if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
            char valueBuffer[32];
            if (SETTINGS.sleepTimeoutMinutes >= CrossPointSettings::SLEEP_TIMEOUT_NEVER_MINUTES) {
              valueText = tr(STR_SLEEP_NEVER);
            } else {
              snprintf(valueBuffer, sizeof(valueBuffer), tr(STR_SLEEP_TIMER_VALUE_FORMAT),
                       static_cast<unsigned int>(SETTINGS.*(setting.valuePtr)));
              valueText = valueBuffer;
            }
          } else if (setting.nameId == StrId::STR_LINE_SPACING ||
                     setting.nameId == StrId::STR_FIRST_LINE_INDENT_PERCENT) {
            valueText = std::to_string(SETTINGS.*(setting.valuePtr)) + "%";
          } else if (setting.nameId == StrId::STR_READING_IDLE_LIMIT) {
            const unsigned seconds = static_cast<unsigned>(SETTINGS.readingStatsIdleSeconds());
            char valueBuffer[32];
            if (seconds < 60) {
              snprintf(valueBuffer, sizeof(valueBuffer), tr(STR_STATS_SECONDS_VALUE), seconds);
            } else if (seconds % 60 == 0) {
              snprintf(valueBuffer, sizeof(valueBuffer), tr(STR_STATS_MINUTES_VALUE), seconds / 60);
            } else {
              snprintf(valueBuffer, sizeof(valueBuffer), tr(STR_STATS_MIN_SEC_VALUE), seconds / 60, seconds % 60);
            }
            valueText = valueBuffer;
          } else {
            valueText = std::to_string(SETTINGS.*(setting.valuePtr));
          }
        }
        return valueText;
      },
      true);

  // Draw help text. Settings that open a sub-picker (enums, line spacing, sleep
  // timer) say "Select"; in-place toggles/values say "Toggle".
  const char* confirmLabel;
  if (selectedSettingIndex == 0) {
    confirmLabel = I18N.get(categoryNames[(selectedCategoryIndex + 1) % categoryCount]);
  } else {
    const auto& sel = (*currentSettings)[selectedSettingIndex - 1];
    const bool opensPicker = sel.type == SettingType::ENUM || sel.nameId == StrId::STR_TIME_TO_SLEEP ||
                             sel.nameId == StrId::STR_LINE_SPACING || sel.nameId == StrId::STR_SCREEN_MARGIN ||
                             sel.nameId == StrId::STR_SCREEN_MARGIN_TOP ||
                             sel.nameId == StrId::STR_SCREEN_MARGIN_BOTTOM ||
                             sel.nameId == StrId::STR_FIRST_LINE_INDENT_PERCENT;
    confirmLabel = opensPicker ? tr(STR_SELECT) : tr(STR_TOGGLE);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
}
