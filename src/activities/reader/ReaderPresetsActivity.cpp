#include "ReaderPresetsActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderPresetMigration.h"
#include "ReaderPresetNames.h"
#include "ReaderPresetStore.h"
#include "SdCardFontSystem.h"
#include "activities/ActivityManager.h"
#include "activities/ActivityResult.h"
#include "activities/network/NearbyFileTransferActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/PresetBundle.h"

namespace fui = freeink::ui;

size_t ReaderPresetsActivity::presetCount() const { return READER_PRESETS.getCount(); }

bool ReaderPresetsActivity::hasSaveRow() const { return !READER_PRESETS.isFull(); }

bool ReaderPresetsActivity::hasSendRow() const { return presetCount() > 0; }

int ReaderPresetsActivity::rowCount() const {
  return static_cast<int>(presetCount()) + (hasSaveRow() ? 1 : 0) + (hasSendRow() ? 1 : 0);
}

bool ReaderPresetsActivity::isSaveRow(const size_t index) const { return hasSaveRow() && index == presetCount(); }

bool ReaderPresetsActivity::isSendRow(const size_t index) const {
  return hasSendRow() && index == presetCount() + (hasSaveRow() ? 1 : 0);
}

int ReaderPresetsActivity::matchingPresetIndex() const {
  const auto& presets = READER_PRESETS.getPresets();
  for (size_t i = 0; i < presets.size(); i++) {
    // ReaderPrefs is trivially-copyable POD with no padding, so the whole blob compares
    // exactly — the same test the reader uses to decide a book has really changed.
    // Applied the same way the reader applies it, so a theme saved without a status bar
    // matches the book whose bar it would leave alone.
    ReaderPrefs applied = presets[i].prefs;
    reader_preset_migration::keepStatusBarWhenAbsent(presets[i].carriesStatusBar, currentPrefs, applied);
    if (std::memcmp(&applied, &currentPrefs, sizeof(ReaderPrefs)) == 0) return static_cast<int>(i);
  }
  return -1;
}

void ReaderPresetsActivity::clampSelector() {
  const int count = rowCount();
  if (count <= 0) {
    nav.selected = 0;
    return;
  }
  if (nav.selected >= count) nav.selected = count - 1;
}

void ReaderPresetsActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
  labels.clear();
}

const char* ReaderPresetsActivity::headerTitle() const { return tr(STR_READING_THEMES); }

// ── Actions ─────────────────────────────────────────────────────────────────

void ReaderPresetsActivity::applyAsGlobalDefault(const size_t index) {
  const ReaderPreset* preset = READER_PRESETS.get(index);
  if (!preset) return;
  ReaderPrefs adopted = preset->prefs;
  if (adopted.sdFontFamilyName[0] != '\0') {
    const auto& families = sdFontSystem.registry().getFamilies();
    const bool installed = std::any_of(families.begin(), families.end(), [&](const SdCardFontFamilyInfo& fam) {
      return fam.name == adopted.sdFontFamilyName;
    });
    if (!installed) {
      LOG_ERR("PRE", "Preset names missing SD font '%s' — falling back to built-in", adopted.sdFontFamilyName);
      std::memset(adopted.sdFontFamilyName, 0, sizeof(adopted.sdFontFamilyName));
      adopted.fontFamily = CrossPointSettings::CHAREINK;
      READER_PRESETS.overwrite(index, adopted);
    }
  }
  SETTINGS.applyGlobalPreset(adopted, preset->carriesStatusBar);
  if (!inBook_) currentPrefs = ReaderPrefs::fromGlobal();
  GUI.drawPopup(renderer, tr(STR_APPLIED_AS_DEFAULT));
  requestUpdate();
}

void ReaderPresetsActivity::startSendNearby() {
  if (presetCount() == 0) return;
  if (inBook_) {
    // Let EpubReaderActivity release the open book before bringing up ESP-NOW.
    setResult(PresetResult{0, /*sendNearby=*/true});
    finish();
    return;
  }
  const std::string path = preset_bundle::STAGING_PATH;
  auto nearby = makeUniqueNoThrow<NearbyFileTransferActivity>(renderer, mappedInput,
                                                              NearbyFileTransferActivity::Mode::Send, path);
  if (!nearby) {
    LOG_ERR("PRE", "OOM: NearbyFileTransferActivity for themes");
    return;
  }
  JsonDocument doc;
  READER_PRESETS.toJson(doc);
  std::string json;
  serializeJson(doc, json);
  Storage.ensureDirectoryExists("/.crosspoint");
  Storage.remove(path.c_str());
  {
    HalFile file;
    if (!Storage.openFileForWrite("PRE", path, file) || !file.isOpen() ||
        file.write(reinterpret_cast<const uint8_t*>(json.data()), json.size()) != json.size()) {
      LOG_ERR("PRE", "Could not write themes bundle");
      file.close();
      Storage.remove(path.c_str());
      return;
    }
    file.close();
  }
  activityManager.replaceFromOpener(std::move(nearby), ActivityManager::Opener::SettingsReadingThemes);
}

void ReaderPresetsActivity::openPresetActions(const size_t index) {
  const ReaderPreset* preset = READER_PRESETS.get(index);
  if (!preset) return;
  // Title by value: the popup keeps its own copy, and the store can change underneath.
  const std::string name = preset->name;
  if (inBook_) {
    const char* const options[] = {tr(STR_APPLY), tr(STR_SET_AS_GLOBAL_DEFAULT), tr(STR_RENAME),
                                   tr(STR_OVERWRITE_WITH_CURRENT), tr(STR_DELETE)};
    optionPopup.show(name.c_str(), options, 5, 0, [this, index](const int choice) {
      switch (choice) {
        case 0:
          // Only the reader can re-lay out the book, so this is the one action that leaves.
          setResult(PresetResult{index, false});
          finish();
          break;
        case 1:
          applyAsGlobalDefault(index);
          break;
        case 2:
          startRename(index);
          break;
        case 3:
          if (!READER_PRESETS.overwrite(index, currentPrefs)) LOG_ERR("PRE", "Could not overwrite reading theme");
          requestUpdate();
          break;
        default:
          confirmDelete(index);
          break;
      }
    });
  } else {
    const char* const options[] = {tr(STR_SET_AS_GLOBAL_DEFAULT), tr(STR_RENAME), tr(STR_OVERWRITE_WITH_CURRENT),
                                   tr(STR_DELETE)};
    optionPopup.show(name.c_str(), options, 4, 0, [this, index](const int choice) {
      switch (choice) {
        case 0:
          applyAsGlobalDefault(index);
          break;
        case 1:
          startRename(index);
          break;
        case 2:
          if (!READER_PRESETS.overwrite(index, currentPrefs)) LOG_ERR("PRE", "Could not overwrite reading theme");
          requestUpdate();
          break;
        default:
          confirmDelete(index);
          break;
      }
    });
  }
  requestUpdate();
}

void ReaderPresetsActivity::startSaveCurrent() {
  if (READER_PRESETS.isFull()) return;
  // Pre-seeded so the user can save with two presses; the store makes the name unique
  // again on add, but seeding the unique one means the field shows what will be saved.
  const std::string suggestion = readerpreset::makeUniqueName(tr(STR_NEW_THEME_NAME), READER_PRESETS.names());
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, std::string(tr(STR_THEME_NAME)), suggestion,
                                              readerpreset::MAX_NAME_LENGTH, InputType::Text),
      [this, suggestion](const ActivityResult& result) {
        if (result.isCancelled) {
          requestUpdate();
          return;
        }
        std::string name;
        if (const auto* kb = std::get_if<KeyboardResult>(&result.data)) name = readerpreset::sanitizeName(kb->text);
        // A theme with no name cannot be picked out of the list, so a blank field takes
        // the suggestion rather than saving nothing or nothing-named.
        if (name.empty()) name = suggestion;
        if (!READER_PRESETS.add(name, currentPrefs)) LOG_ERR("PRE", "Could not save reading theme");
        clampSelector();
        requestUpdate();
      });
}

void ReaderPresetsActivity::startRename(const size_t index) {
  const ReaderPreset* preset = READER_PRESETS.get(index);
  if (!preset) return;
  const std::string existing = preset->name;
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, std::string(tr(STR_THEME_NAME)), existing,
                                              readerpreset::MAX_NAME_LENGTH, InputType::Text),
      [this, index, existing](const ActivityResult& result) {
        if (result.isCancelled) {
          requestUpdate();
          return;
        }
        std::string name;
        if (const auto* kb = std::get_if<KeyboardResult>(&result.data)) name = readerpreset::sanitizeName(kb->text);
        if (name.empty()) name = existing;  // clearing the field keeps the name it had
        if (!READER_PRESETS.rename(index, name)) LOG_ERR("PRE", "Could not rename reading theme");
        requestUpdate();
      });
}

void ReaderPresetsActivity::confirmDelete(const size_t index) {
  const ReaderPreset* preset = READER_PRESETS.get(index);
  if (!preset) return;
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, std::string(tr(STR_DELETE_THEME)), preset->name),
      [this, index](const ActivityResult& result) {
        if (!result.isCancelled) {
          if (!READER_PRESETS.remove(index)) LOG_ERR("PRE", "Could not delete reading theme");
          clampSelector();
        }
        requestUpdate();
      });
}

void ReaderPresetsActivity::activateIndex(const int index) {
  app.clearTapFlash();
  const size_t row = static_cast<size_t>(index);
  if (isSaveRow(row)) {
    startSaveCurrent();
    return;
  }
  if (isSendRow(row)) {
    startSendNearby();
    return;
  }
  if (row >= presetCount()) return;
  openPresetActions(row);
}

void ReaderPresetsActivity::onBackButton() {
  ActivityResult res;
  res.isCancelled = true;
  setResult(std::move(res));
  finish();
}

// ── Lifecycle ───────────────────────────────────────────────────────────────

bool ReaderPresetsActivity::handleCustomInput() {
  // The pop-up also swallows the release of the press that closed it.
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

const char* ReaderPresetsActivity::noteText() const {
  // One status line above the list: nothing saved yet, or no slot left to save into.
  // The list itself is never empty — it always holds either themes or the save row.
  if (presetCount() == 0) return tr(STR_READING_THEMES_NONE);
  if (!hasSaveRow()) return tr(STR_READING_THEMES_FULL);
  return nullptr;
}

ListChrome ReaderPresetsActivity::chrome() const {
  ListChrome chrome;
  chrome.title = tr(STR_READING_THEMES);
  chrome.note = noteText();
  return chrome;
}

void ReaderPresetsActivity::buildScreen(UiScreen& screen) {
  const int count = rowCount();
  if (count <= 0) return;

  const int matching = matchingPresetIndex();
  const auto& presets = READER_PRESETS.getPresets();
  labels.assign(static_cast<size_t>(count), std::string());
  rows.assign(static_cast<size_t>(count), fui::ListItem{});
  for (int i = 0; i < count; ++i) {
    const size_t row = static_cast<size_t>(i);
    if (isSaveRow(row)) {
      labels[i] = std::string(tr(STR_SAVE_CURRENT_LOOK));
    } else if (isSendRow(row)) {
      labels[i] = std::string(tr(STR_SEND_THEMES_NEARBY));
    } else {
      labels[i] = presets[i].name;
    }
    rows[i].label = labels[i].c_str();
    // The book's own settings are marked in the value column rather than in the name,
    // so the name stays what the user typed and the mark moves as the book changes.
    if (i == matching) rows[i].value = tr(STR_CURRENT);
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps props{};
  props.items = rows.data();
  props.count = static_cast<uint16_t>(count);
  props.action = ACTION_ROW;
  syncListViewport(screen, props);
  screen.list(props);
}

bool ReaderPresetsActivity::drawOverlay() {
  // Drawn over the finished list, so the popup reads as sitting on it.
  return optionPopup.processRender(renderer, mappedInput);
}
