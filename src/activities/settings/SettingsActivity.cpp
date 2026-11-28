#include "SettingsActivity.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "AllBookmarksActivity.h"
#include "ButtonBindingsActivity.h"
#include "ButtonRemapActivity.h"
#include "ClearCacheActivity.h"
#include "CrossPointSettings.h"
#include "FontDownloadActivity.h"
#include "InstalledFontsActivity.h"
#include "KOReaderSettingsActivity.h"
#include "MappedInputManager.h"
#include "OpdsServerListActivity.h"
#include "OpdsServerStore.h"
#include "OtaUpdateActivity.h"
#include "PopupItemsActivity.h"
#include "ScreenCalibrationActivity.h"
#include "SdCardFontSystem.h"
#include "SdFirmwareUpdateActivity.h"
#include "SettingsList.h"
#include "SilentRestart.h"
#include "StatusBarSettingsActivity.h"
#include "TextSettingsActivity.h"
#include "TimezonePickerActivity.h"
#include "WifiCredentialStore.h"
#include "activities/network/CrossPointWebServerActivity.h"
#include "activities/network/NearbyFileTransferActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/reader/ReaderPresetsActivity.h"
#include "activities/settings/SettingsListNav.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/BusyBanner.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "sleep/SleepWallpaperIndexStore.h"
#include "util/CredentialBundle.h"
#include "util/ReleaseStorage.h"

namespace {

// One section of the list: the heading, then the rows under it in display order.
struct SettingsGroup {
  StrId heading;
  std::vector<StrId> members;
};

// Reorders a category's rows into the given sections and inserts a heading above each.
// The group map, not SettingsList.h's declaration order, decides what the user sees.
//
// A group whose rows are all absent contributes no heading, which is what keeps the
// headings honest when a conditional row drops out (Quick-return from footnotes, Pop-up
// Items, Screen Cleanup on X4) or when a whole group is board-specific.
//
// Rows the map does not mention keep their relative order and are appended at the end
// under no heading. That is deliberate: a setting added to SettingsList.h later stays
// reachable on the device even if whoever added it never touched this map. The log line
// is what fails the sim's Settings test when that happens.
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
    if (placed[i]) continue;
    LOG_ERR("SET", "Row not in a group map: %s", I18N.get(rows[i].nameId));
    grouped.push_back(rows[i]);
  }

  rows.swap(grouped);
}

}  // namespace

namespace {
// Four settings categories, then the File Transfer activity: the hub's five cards.
constexpr int kCategoryCount = 5;
constexpr int kSystem = 3;
constexpr int kFileTransfer = 4;
// System's rare rows, one level down from it. Not a hub card.
constexpr int kMaintenance = 5;
}  // namespace

std::string SettingsActivity::settingValueText(const SettingInfo& setting) const {
  std::string valueText;
  if (setting.action == SettingAction::TimeZone) {
    return clock_zones::TABLE[clock_zones::activeIndex(SETTINGS.clockTimezone)].name;
  }
  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    return SETTINGS.*(setting.valuePtr) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  }
  if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    return I18N.get(setting.enumValues[SETTINGS.*(setting.valuePtr)]);
  }
  if (setting.type == SettingType::ENUM && setting.valueGetter) {
    const uint8_t value = setting.valueGetter();
    if (!setting.enumStringValues.empty() && value < setting.enumStringValues.size()) {
      return setting.enumStringValues[value];
    }
    if (value < setting.enumValues.size()) return I18N.get(setting.enumValues[value]);
    return {};
  }
  if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    char valueBuffer[32];
    if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
      if (SETTINGS.sleepTimeoutMinutes >= CrossPointSettings::SLEEP_TIMEOUT_NEVER_MINUTES) return tr(STR_SLEEP_NEVER);
      snprintf(valueBuffer, sizeof(valueBuffer), tr(STR_SLEEP_TIMER_VALUE_FORMAT),
               static_cast<unsigned int>(SETTINGS.*(setting.valuePtr)));
      return valueBuffer;
    }
    return std::to_string(setting.shownValue(SETTINGS.*(setting.valuePtr)));
  }
  if (setting.type == SettingType::STRING) {
    if (setting.stringGetter) return setting.stringGetter();
    if (setting.stringMaxLen > 0) {
      return reinterpret_cast<const char*>(reinterpret_cast<const uint8_t*>(&SETTINGS) + setting.stringOffset);
    }
  }
  return valueText;
}

StrId SettingsActivity::categoryName(const int index) const {
  switch (index) {
    case kMaintenance:
      return StrId::STR_MAINTENANCE;
    case kFileTransfer:
      return StrId::STR_FILE_TRANSFER;
    case 0:
      return StrId::STR_CAT_DISPLAY;
    case 1:
      return StrId::STR_CAT_READER;
    case 2:
      return StrId::STR_CAT_CONTROLS;
    default:
      return StrId::STR_CAT_SYSTEM;
  }
}

std::vector<SettingInfo>& SettingsActivity::categoryRows(const int index) {
  switch (index) {
    case 0:
      return displaySettings;
    case 1:
      return readerSettings;
    case 2:
      return controlsSettings;
    case kMaintenance:
      return maintenanceSettings;
    default:
      return systemSettings;
  }
}

// The chosen category's rows under their group headings. The two-column grid of the other
// touch boards has no heading look, so it keeps dropping them.
void SettingsActivity::selectCategory(const int index) {
  {
    RenderLock lock(*this);
    selectedCategory = std::clamp(index, 0, kMaintenance);
    settings.clear();
    const bool headings = usesWrappedRows();
    for (const auto& row : categoryRows(selectedCategory)) {
      if (row.isHeader && !headings) continue;
      settings.push_back(row);
    }
    settingsCount = static_cast<int>(settings.size());
  }
  setSelected(0);
}

UiGridActivity::CellLook SettingsActivity::cellLook(const int index) const {
  if (mode == Mode::Hub) return CellLook::Card;
  return index >= 0 && index < settingsCount && settings[index].isHeader ? CellLook::Header : CellLook::Row;
}

int SettingsActivity::cellCount() const { return mode == Mode::Hub ? kCategoryCount : settingsCount; }

const char* SettingsActivity::cellName(const int index) const {
  if (mode == Mode::Hub) {
    if (index < 0 || index >= kCategoryCount) return nullptr;
    cellNameScratch = I18N.get(categoryName(index));
    return cellNameScratch.c_str();
  }
  if (index < 0 || index >= settingsCount) return nullptr;
  cellNameScratch = I18N.get(settings[index].nameId);
  return cellNameScratch.c_str();
}

const char* SettingsActivity::cellValue(const int index) const {
  if (mode == Mode::Hub) {
    if (index < 0 || index >= kCategoryCount) return nullptr;
    cellValueScratch = hubSummary(index);
    return cellValueScratch.c_str();
  }
  if (index < 0 || index >= settingsCount || settings[index].isHeader) return nullptr;
  cellValueScratch = settingValueText(settings[index]);
  return cellValueScratch.c_str();
}

void SettingsActivity::rebuildSettingsList() {
  // Built per category so each keeps its own grouping map; selectCategory copies the
  // open category's rows into `settings`.
  displaySettings.clear();
  readerSettings.clear();
  controlsSettings.clear();
  systemSettings.clear();
  maintenanceSettings.clear();

  // Pick up any fonts uploaded/deleted over the web server since the last
  // reader activity ran — otherwise the font-family picker shows stale list.
  sdFontSystem.refreshIfDirty();

  // Rescan /dictionaries on every rebuild: cheap (one directory listing) and
  // picks up dictionaries copied to the SD card since the last visit.
  std::vector<DictionaryEntry> dictionaries;
  DictionaryRegistry::discover(dictionaries);

  for (auto& setting : getSettingsList(&sdFontSystem.registry(), &dictionaries)) {
    if (setting.category == StrId::STR_NONE_OPT) continue;
    // The light panel owns the frontlight and the Time Zone screen owns the zone; their rows
    // stay in the shared list for the web page.
    if (setting.webOnly) continue;
    if (setting.category == StrId::STR_CAT_DISPLAY) {
      // Every sleep is already Quick Resume when that face is selected.
      if (setting.valuePtr == &CrossPointSettings::quickResumeSleepScreen &&
          SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME) {
        continue;
      }
      displaySettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_READER) {
      // Settings merged into "Text Settings"
      // (they stay in the shared list for the web settings API)
      if (setting.inTextSettings) continue;
      readerSettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_CONTROLS) {
      // The per-button bindings live in the Buttons screen; they stay in the shared
      // list only so they persist and reach the web settings API.
      if (setting.inButtons) continue;
      // Only means anything while Footnotes is on one of the power button's in-book
      // gestures: it says whether that same gesture walks back out of the footnote.
      if (setting.valuePtr == &CrossPointSettings::pwrBtnFootnoteBack && !SETTINGS.powerOpensFootnotes()) {
        continue;
      }
      // Touch reader settings only mean something on a board with a digitiser.
      if ((setting.valuePtr == &CrossPointSettings::touchReaderControls ||
           setting.valuePtr == &CrossPointSettings::pageTurnGesture ||
           setting.valuePtr == &CrossPointSettings::previousPageGesture ||
           setting.valuePtr == &CrossPointSettings::showReaderMenu) &&
          !gpio.hasTouch()) {
        continue;
      }
      controlsSettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_SYSTEM) {
      systemSettings.push_back(setting);
    }
  }

  // Append ACTION items. Position here does not decide what the screen shows: applyGroups
  // below rebuilds every category from its group map, and each of these rows is named
  // there.
  if (gpio.isXteinkDevice()) {
    controlsSettings.push_back(SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons));
  }
  controlsSettings.push_back(SettingInfo::Action(StrId::STR_BUTTONS, SettingAction::Buttons));
  // Pop-up Items only exists to serve a binding set to Menu Pop-up, so it is offered
  // only while at least one of the three bindings actually opens one.
  if (SETTINGS.anyBindingOpensPopup()) {
    controlsSettings.push_back(SettingInfo::Action(StrId::STR_POPUP_ITEMS, SettingAction::PopupItems));
  }
  systemSettings.push_back(SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_OPDS_SERVERS, SettingAction::OPDSBrowser));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_SHARE_CREDENTIALS, SettingAction::ShareCredentials));
  displaySettings.push_back(SettingInfo::Action(StrId::STR_SHUFFLE_WALLPAPERS, SettingAction::ShuffleWallpapers));
  displaySettings.push_back(SettingInfo::Action(StrId::STR_SCREEN_CALIBRATION, SettingAction::ScreenCalibration));
  // The rows a reader reaches for a few times a year, one level down in Maintenance, so
  // System's daily rows are not walled in by them. In the order the sub-list shows them.
  systemSettings.push_back(SettingInfo::Action(StrId::STR_MAINTENANCE, SettingAction::Maintenance));
  maintenanceSettings.push_back(SettingInfo::Action(StrId::STR_CLEAN_STORAGE, SettingAction::CleanStorage));
  maintenanceSettings.push_back(SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache));
  maintenanceSettings.push_back(SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates));
  maintenanceSettings.push_back(SettingInfo::Action(StrId::STR_SD_FIRMWARE_UPDATE, SettingAction::SdFirmwareUpdate));
  // Deliberately its own row rather than a branch inside Check for Updates: on a
  // device whose USB flashing the vendor locked, this is the only way to put
  // another firmware on it, and it must be findable without first being told
  // there is no update.
  maintenanceSettings.push_back(
      SettingInfo::Action(StrId::STR_INSTALL_OTHER_FIRMWARE, SettingAction::InstallOtherFirmware));
  if (halClock.isAvailable()) {
    systemSettings.push_back(SettingInfo::Action(StrId::STR_TIMEZONE, SettingAction::TimeZone));
  }
  readerSettings.push_back(SettingInfo::Action(StrId::STR_TEXT_SETTINGS, SettingAction::TextSettings));
  readerSettings.push_back(SettingInfo::Action(StrId::STR_ALL_BOOKMARKS, SettingAction::AllBookmarks));
  readerSettings.push_back(SettingInfo::Action(StrId::STR_READING_THEMES, SettingAction::ReadingThemes));
  readerSettings.push_back(SettingInfo::Action(StrId::STR_MANAGE_FONTS, SettingAction::DownloadFonts));
  readerSettings.push_back(SettingInfo::Action(StrId::STR_INSTALLED_FONTS, SettingAction::InstalledFonts));
  readerSettings.push_back(SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar));

  // Section headings. Applied last so the ACTION rows spliced in above are grouped
  // alongside the settings they belong with rather than stranded at the ends.
  // The screen itself leads, then the two sleep-screen groups, which are set up once and
  // revisited only when the wallpapers change. The frontlight has no rows here: the light
  // panel owns on/off, brightness and warmth, and the rest is on the web settings page.
  applyGroups(displaySettings,
              {
                  {StrId::STR_GRP_SCREEN,
                   {StrId::STR_REFRESH_FREQ, StrId::STR_CHAPTER_START_REFRESH, StrId::STR_CHAPTER_WIPE,
                    StrId::STR_SUNLIGHT_FADING_FIX, StrId::STR_HIDE_BATTERY, StrId::STR_SCREEN_CALIBRATION}},
                  {StrId::STR_GRP_SLEEP_SCREEN,
                   {StrId::STR_SLEEP_SCREEN, StrId::STR_QUICK_RESUME_TIMEOUT, StrId::STR_FAST_UNLOCK}},
                  {StrId::STR_GRP_WALLPAPER,
                   {StrId::STR_SLEEP_COVER_MODE, StrId::STR_SLEEP_COVER_FILTER, StrId::STR_SHOW_SLEEP_IMAGE_FILENAME,
                    StrId::STR_SHOW_SLEEP_WALLPAPER_POSITION, StrId::STR_SHUFFLE_WALLPAPERS}},
                  {StrId::STR_GRP_HOME, {StrId::STR_AUTHOR_DISPLAY}},
              });

  applyGroups(
      readerSettings,
      {
          {StrId::STR_GRP_TEXT,
           {StrId::STR_TEXT_SETTINGS, StrId::STR_MANAGE_FONTS, StrId::STR_INSTALLED_FONTS, StrId::STR_DICTIONARY}},
          {StrId::STR_GRP_PAGE,
           {StrId::STR_ORIENTATION, StrId::STR_PARAGRAPH_NUMBERS, StrId::STR_PARAGRAPH_NUMBER_SIZE,
            StrId::STR_PARAGRAPH_NUMBER_PLACE}},
          // The status bar rows adjacent, and the screen that configures it straight after
          // the toggle that hides it.
          {StrId::STR_GRP_LOOK,
           {StrId::STR_PAPERBACK_LOOK, StrId::STR_PAPERBACK_STATUS, StrId::STR_RULED_LINES,
            StrId::STR_CUSTOMISE_STATUS_BAR, StrId::STR_NIGHT_MODE, StrId::STR_READER_MENU_STYLE,
            StrId::STR_READING_THEMES}},
          {StrId::STR_GRP_LIBRARY, {StrId::STR_ALL_BOOKMARKS}},
      });

  applyGroups(controlsSettings,
              {
                  // Touch leads on a board that has it: it is how that reader is driven all day.
                  // On a board without a digitiser both rows are absent and the heading with them.
                  {StrId::STR_GRP_TOUCH,
                   {StrId::STR_TOUCH_READER_CONTROLS, StrId::STR_NEXT_PAGE_GESTURE, StrId::STR_PREV_PAGE_GESTURE,
                    StrId::STR_SHOW_READER_MENU}},
                  {StrId::STR_GRP_BACK, {StrId::STR_BACK_SHORT_TO_FILE_BROWSER, StrId::STR_HOME_BACK_ACTION}},
                  // Buttons leads its group: it holds the eighteen per-button bindings, and left
                  // out of the map it fell to the bottom of the category under no heading.
                  {StrId::STR_GRP_BUTTONS,
                   {StrId::STR_BUTTONS, StrId::STR_REMAP_FRONT_BUTTONS, StrId::STR_SIDE_BTN_LAYOUT,
                    StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION}},
                  // What is left of this group now the power button's own bindings moved into the
                  // Buttons screen: how long a wake hold is, and whether the footnote binding also
                  // walks back out.
                  {StrId::STR_GRP_POWER_BUTTON, {StrId::STR_PWR_BTN_FOOTNOTE_BACK}},
                  // Pop-up Items sits with the bindings, because it only configures what the
                  // pop-up those bindings open actually contains.
                  {StrId::STR_GRP_HOLD, {StrId::STR_LONG_PRESS_MENU, StrId::STR_MENU_HOLD, StrId::STR_POPUP_ITEMS}},
              });

  applyGroups(systemSettings,
              {
                  {StrId::STR_GRP_POWER, {StrId::STR_TIME_TO_SLEEP, StrId::STR_TOUCH_SLEEP}},
                  {StrId::STR_GRP_LIBRARY,
                   {StrId::STR_LIBRARY_USE_METADATA, StrId::STR_SHOW_HIDDEN_FILES, StrId::STR_BOOK_BROWSER_ORDER,
                    StrId::STR_OPEN_BOOK_ON_BOOT, StrId::STR_REMOVE_READ_FROM_RECENTS, StrId::STR_MOVE_FINISHED_TO_READ,
                    StrId::STR_MOVE_OPENED_TO_RECENTS}},
                  // Device Name leads: it is the name this reader broadcasts to another one during
                  // Nearby Position Sync, so it belongs with the network rows rather than stranded
                  // at the bottom of the category, which is where it sat.
                  {StrId::STR_GRP_NETWORK,
                   {StrId::STR_DEVICE_NAME, StrId::STR_WIFI_NETWORKS, StrId::STR_KOREADER_SYNC, StrId::STR_OPDS_SERVERS,
                    StrId::STR_SHARE_CREDENTIALS}},
                  {StrId::STR_GRP_DEVICE,
                   {StrId::STR_TIMEZONE, StrId::STR_CLOCK_DST, StrId::STR_WAKE_ON_USB_PLUG_IN, StrId::STR_MAINTENANCE}},
              });
}

std::unique_ptr<SettingsActivity> SettingsActivity::onFileTransferCard(GfxRenderer& renderer,
                                                                       MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<SettingsActivity>(renderer, mappedInput, kFileTransfer);
}

std::unique_ptr<SettingsActivity> SettingsActivity::onShareCredentials(GfxRenderer& renderer,
                                                                       MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<SettingsActivity>(renderer, mappedInput, kSystem, kSystem, SettingAction::ShareCredentials);
}

std::unique_ptr<SettingsActivity> SettingsActivity::onReadingThemes(GfxRenderer& renderer,
                                                                    MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<SettingsActivity>(renderer, mappedInput, 1, 1, SettingAction::ReadingThemes);
}

// A restart step (ActivityManager::backTrail): (category + 1) << 8, then the row's action in a
// category or the card on the hub.
bool SettingsActivity::restartStep(uint8_t& opener, int& row) const {
  opener = static_cast<uint8_t>(ActivityManager::Opener::Settings);
  const int index = selected();
  if (mode == Mode::Hub || index < 0 || index >= settingsCount) {
    row = mode == Mode::Hub ? index : (selectedCategory + 1) << 8;
    return true;
  }
  row = (selectedCategory + 1) << 8 | (static_cast<int>(settings[index].action) & 0xFF);
  return true;
}

std::unique_ptr<SettingsActivity> SettingsActivity::fromRestartRow(GfxRenderer& renderer,
                                                                   MappedInputManager& mappedInput, const int row) {
  const int category = (row >> 8) - 1;
  if (category < 0 || category > kMaintenance)
    return makeUniqueNoThrow<SettingsActivity>(renderer, mappedInput, row & 0xFF);
  return makeUniqueNoThrow<SettingsActivity>(renderer, mappedInput, category == kMaintenance ? kSystem : category,
                                             category, static_cast<SettingAction>(row & 0xFF));
}

void SettingsActivity::onEnter() {
  UiGridActivity::onEnter();

  // Opening Settings rescans SD fonts and dictionaries before anything is drawn,
  // so on a loaded card this is a silent wait on the previous screen. Armed only
  // here, not around the rebuilds triggered by changing a setting: those must stay
  // instant, and flashing a banner on every toggle would be worse than nothing.
  BusyBanner banner(renderer, tr(STR_BUSY_LOADING_SETTINGS));

  rebuildSettingsList();
  // Opens on the hub: the categories and File Transfer are the first thing to say now that
  // the group headings live inside them.
  mode = Mode::Hub;
  selectedCategory = 0;
  setSelected(openHubCell);
  if (openCategory >= 0) {
    mode = Mode::Category;
    selectCategory(openCategory);
    for (int i = 0; i < settingsCount; ++i) {
      if (settings[i].action == openRow) setSelected(i);
    }
  }

  requestUpdate();
}

// Saved on every way out (Back, Home gesture, sleep), not only Back: toggles and cycles
// change SETTINGS in place. An unchanged file is skipped by writeDocToFile.
void SettingsActivity::onExit() {
  SETTINGS.saveToFile();
  Activity::onExit();
  // The Wi-Fi Networks screen has no session of its own, and its result handler never
  // runs when Home or a bound key replaces the whole stack. Left up, the radio stays
  // joined and holds the CPU at full clock until the next sleep. No-op when Wi-Fi is off.
  teardownWifiAndRestart();
}

void SettingsActivity::startDownloadActivity(std::unique_ptr<Activity> activity) {
  if (!activity) {
    LOG_ERR("SET", "No memory for download activity");
    return;
  }
  {
    RenderLock lock(*this);
    // Settings stays on the activity stack. None of these cached rows is used
    // by OTA/fonts, and settings retains capacity for ALL categories even when
    // it only displays System. Destruction also frees each row's enum/callback data.
    closeRouting();
    for (auto* rows :
         {&settings, &displaySettings, &readerSettings, &controlsSettings, &systemSettings, &maintenanceSettings}) {
      releaseStorage(*rows);
    }
    settingsCount = 0;
    releaseStorage(cellNameScratch);
    releaseStorage(cellValueScratch);
    releaseStorage(optionPopup);
  }
  startActivityForResult(std::move(activity), [this](const ActivityResult&) {
    SETTINGS.saveToFile();
    {
      RenderLock lock(*this);
      rebuildSettingsList();
    }
    restoreCursorAfterRebuild();
  });
}

bool SettingsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

// A tap or Confirm on a hub cell opens that category; on a settings cell it acts
// on the setting.
void SettingsActivity::activateCell(const int index) {
  if (mode == Mode::Hub) {
    // File Transfer is a hub destination, not a category of persisted settings.
    if (index == kFileTransfer) {
      // Replaces this screen (the web server wants its RAM); Back rebuilds it on this card.
      auto transfer = makeUniqueNoThrow<CrossPointWebServerActivity>(renderer, mappedInput);
      activityManager.replaceFromOpener(std::move(transfer), ActivityManager::Opener::SettingsFileTransfer);
      return;
    }
    // A new screen: its first row shares this cell's action and value, so it would
    // inherit the tap flash.
    app.clearTapFlash();
    // Category first: selectCategory() lands the cursor by the category's own cell looks.
    mode = Mode::Category;
    selectCategory(index);
    requestUpdate();
    return;
  }
  toggleCurrentSetting();
  requestUpdate();
}

void SettingsActivity::onBackButton() {
  // Back inside a category returns to the hub; from the hub it leaves the screen.
  if (mode == Mode::Category && selectedCategory == kMaintenance) {
    // Maintenance is a row of System, so Back lands on that row.
    selectCategory(kSystem);
    for (int i = 0; i < settingsCount; ++i) {
      if (settings[i].action == SettingAction::Maintenance) setSelected(i);
    }
    return;
  }
  if (mode == Mode::Category) {
    mode = Mode::Hub;
    setSelected(selectedCategory);
    return;
  }
  finish();  // back to the screen under it, or Home on the Settings row
}

// What Confirm does to this row, for the band: a switch says which way it goes, a choice
// says it changes, and anything else opens.
static const char* confirmVerb(const SettingInfo& setting) {
  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    return SETTINGS.*(setting.valuePtr) ? tr(STR_TURN_OFF) : tr(STR_TURN_ON);
  }
  if (setting.type == SettingType::ACTION) return tr(STR_OPEN);
  return tr(STR_CHANGE);
}

ListChrome SettingsActivity::chrome() const {
  ListChrome chrome;
  chrome.title = mode == Mode::Hub ? tr(STR_SETTINGS_TITLE) : I18N.get(categoryName(selectedCategory));
  if (mode == Mode::Hub) chrome.footerRight = CROSSPOINT_VERSION;

  const int index = selected();
  const bool onSetting = mode == Mode::Category && index >= 0 && index < settingsCount && !settings[index].isHeader;
  chrome.confirmHint = onSetting ? confirmVerb(settings[index]) : tr(STR_OPEN);
  return chrome;
}

bool SettingsActivity::drawOverlay() { return optionPopup.processRender(renderer, mappedInput); }

void SettingsActivity::toggleCurrentSetting() {
  const int selectedSetting = selected();
  if (selectedSetting < 0 || selectedSetting >= settingsCount) {
    return;
  }
  const auto& setting = settings[selectedSetting];
  if (setting.isHeader) return;

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
                       [this, valuePtr, offeredValues](int idx) {
                         if (idx < 0 || idx >= static_cast<int>(offeredValues.size())) return;
                         SETTINGS.*valuePtr = offeredValues[idx];
                         SETTINGS.saveToFile();
                         rebuildSettingsList();
                         restoreCursorAfterRebuild();
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
      auto onSelect = [this, valueSetter](int idx) {
        valueSetter(idx);
        SETTINGS.saveToFile();
        rebuildSettingsList();
        restoreCursorAfterRebuild();
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
    // Front Left/Right always step by one, whatever the setting declares: a row that can
    // only be set to a multiple of five cannot be set to the value between them, and a
    // brightness or a margin is exactly where that one unit is worth having. The side
    // buttons carry the setting's own step (five, where it has one) so a wide range is
    // still crossed in a few presses.
    const auto valuePtr = setting.valuePtr;
    constexpr int minLargeStep = 5;
    auto dialog = makeUniqueNoThrow<IntervalSelectionActivity>(
        renderer, mappedInput, "SettingValue", setting.nameId, setting.shownValue(SETTINGS.*(setting.valuePtr)),
        setting.shownValue(setting.valueRange.min), setting.shownValue(setting.valueRange.max), /*smallStep=*/1,
        std::max(minLargeStep, static_cast<int>(setting.valueRange.step)));
    if (!dialog) {
      LOG_ERR("SET", "OOM: IntervalSelectionActivity");
      return;
    }
    const int offset = setting.displayOffset;
    startActivityForResult(std::move(dialog), [this, valuePtr, offset](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto* chosen = std::get_if<IntervalResult>(&result.data);
        if (chosen != nullptr) SETTINGS.*valuePtr = static_cast<uint8_t>(chosen->value - offset);
        // One write when the dialog closes, not one per step: the value moved
        // through every number between the two ends on the way here.
        SETTINGS.saveToFile();
      }
      rebuildSettingsList();
      restoreCursorAfterRebuild();
      requestUpdate();
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
            rebuildSettingsList();
            restoreCursorAfterRebuild();
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
        startActivityForResult(std::make_unique<StatusBarSettingsActivity>(
                                   renderer, mappedInput, SETTINGS.statusBar(),
                                   [](void*, const StatusBarBlock& b) {
                                     SETTINGS.setStatusBar(b);
                                     SETTINGS.saveToFile();
                                   },
                                   nullptr),
                               resultHandler);
        break;
      case SettingAction::Buttons:
        startActivityForResult(std::make_unique<ButtonBindingsActivity>(renderer, mappedInput), resultHandler);
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
      case SettingAction::ShareCredentials:
        shareCredentials();
        break;
      case SettingAction::ReadingThemes:
        if (auto themes = makeUniqueNoThrow<ReaderPresetsActivity>(renderer, mappedInput, ReaderPrefs::fromGlobal(),
                                                                   /*inBook=*/false)) {
          startActivityForResult(std::move(themes), [this](const ActivityResult&) {
            rebuildSettingsList();
            restoreCursorAfterRebuild();
          });
        } else {
          LOG_ERR("SET", "OOM: ReaderPresetsActivity");
        }
        break;
      case SettingAction::Network:
        startActivityForResult(
            std::make_unique<WifiSelectionActivity>(renderer, mappedInput, false), [](const ActivityResult&) {
              SETTINGS.saveToFile();
              // Every other WiFi screen hands the radio to a session whose onExit()
              // tears it down. This one only saves credentials, yet the scan alone
              // brings the driver up, so release its heap here. Runs from the loop
              // task with no lock held; the restart popup paints to the panel.
              RenderLock lock;
              teardownWifiAndRestart(+[] { silentRestartToTrail(activityManager.backTrail(/*withTop=*/true)); });
            });
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
        startActivityForResult(std::make_unique<ClearCacheActivity>(renderer, mappedInput, /*orphansOnly=*/true),
                               resultHandler);
        break;
      case SettingAction::ClearCache:
        startActivityForResult(std::make_unique<ClearCacheActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CheckForUpdates:
        startDownloadActivity(makeUniqueNoThrow<OtaUpdateActivity>(renderer, mappedInput));
        break;
      case SettingAction::InstallOtherFirmware:
        startDownloadActivity(
            makeUniqueNoThrow<OtaUpdateActivity>(renderer, mappedInput, /*installOtherFirmware=*/true));
        break;
      case SettingAction::SdFirmwareUpdate:
        startActivityForResult(std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::ScreenCalibration:
        startActivityForResult(std::make_unique<ScreenCalibrationActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::AllBookmarks:
        startActivityForResult(std::make_unique<AllBookmarksActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::DownloadFonts:
        startDownloadActivity(makeUniqueNoThrow<FontDownloadActivity>(renderer, mappedInput));
        break;
      case SettingAction::InstalledFonts:
        startActivityForResult(std::make_unique<InstalledFontsActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                 SETTINGS.saveToFile();
                                 rebuildSettingsList();
                                 restoreCursorAfterRebuild();
                               });
        break;
      case SettingAction::TextSettings:
        startActivityForResult(std::make_unique<TextSettingsActivity>(
                                   renderer, mappedInput, &sdFontSystem.registry(), ReaderPrefs::fromGlobal(),
                                   [](void*, const ReaderPrefs& edited) {
                                     SETTINGS.applyReaderPrefs(edited);
                                     SETTINGS.saveToFile();
                                   },
                                   nullptr),
                               [this](const ActivityResult&) {
                                 // TextSettingsActivity saves on each change; no save needed here.
                                 rebuildSettingsList();
                                 restoreCursorAfterRebuild();
                               });
        break;
      case SettingAction::Maintenance:
        app.clearTapFlash();
        selectCategory(kMaintenance);
        requestUpdate();
        break;
      case SettingAction::TimeZone:
        // About 6 KB of rows, so an allocation that can fail rather than abort.
        if (auto picker = makeUniqueNoThrow<TimezonePickerActivity>(renderer, mappedInput)) {
          startActivityForResult(std::move(picker), [this](const ActivityResult&) {
            SETTINGS.saveToFile();
            requestUpdate();
          });
        } else {
          LOG_ERR("SET", "OOM: TimezonePickerActivity");
        }
        break;
      case SettingAction::None:
        // Do nothing
        break;
    }
    return;  // Results will be handled in the result handler, so we can return early here
  } else {
    return;
  }

  SETTINGS.saveToFile();
  rebuildSettingsList();
  restoreCursorAfterRebuild();
}

void SettingsActivity::restoreCursorAfterRebuild() {
  // rebuildSettingsList refills the four category vectors, so the active category's cells
  // have to be taken from them again or the grid would keep drawing the old ones.
  // A change can also add or remove cells (Quick-return from footnotes, Pop-up Items),
  // which moves everything after it; the cursor is clamped rather than chased, since the
  // cell it was on may no longer exist.
  const int wasSelected = selected();
  selectCategory(selectedCategory);
  setSelected(std::clamp(wasSelected, 0, std::max(0, settingsCount - 1)));
}

void SettingsActivity::shareCredentials() {
  credential_bundle::Bundle bundle;
  for (size_t i = 0; i < WIFI_STORE.getCredentialCount(); i++) {
    const auto credential = WIFI_STORE.getCredentialAt(i);
    if (!credential || credential->ssid.empty()) continue;
    if (bundle.wifi.size() >= credential_bundle::MAX_ENTRIES) break;
    bundle.wifi.push_back({credential->ssid, credential->password});
  }
  for (const auto& server : OPDS_STORE.getServers()) {
    if (server.url.empty()) continue;
    if (bundle.opds.size() >= credential_bundle::MAX_ENTRIES) break;
    bundle.opds.push_back({server.name, server.url, server.username, server.password});
  }

  if (bundle.empty()) {
    // drawPopup pushes its own refresh, so the banner is already on the panel.
    GUI.drawPopup(renderer, tr(STR_NOTHING_TO_SHARE));
    delay(1200);
    requestUpdate(true);
    return;
  }

  // The Nearby screen owns the radio for its lifetime and removes the bundle in its
  // onExit. It is built first: with no screen to remove it, the bundle is never written.
  const std::string path = credential_bundle::STAGING_PATH;
  auto nearby = makeUniqueNoThrow<NearbyFileTransferActivity>(renderer, mappedInput,
                                                              NearbyFileTransferActivity::Mode::Send, path);
  if (!nearby) {
    LOG_ERR("SET", "OOM: no Nearby screen to share credentials");
    return;
  }

  const std::string json = credential_bundle::serialize(bundle);
  Storage.ensureDirectoryExists("/.crosspoint");
  Storage.remove(path.c_str());
  {
    HalFile file;
    if (!Storage.openFileForWrite("SET", path, file) || !file.isOpen() ||
        file.write(reinterpret_cast<const uint8_t*>(json.data()), json.size()) != json.size()) {
      LOG_ERR("SET", "Could not write the credential bundle");
      file.close();
      Storage.remove(path.c_str());
      return;
    }
    file.close();
  }

  activityManager.replaceFromOpener(std::move(nearby), ActivityManager::Opener::SettingsShareCredentials);
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
