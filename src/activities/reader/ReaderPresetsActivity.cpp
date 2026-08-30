#include "ReaderPresetsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>
#include <string>

#include "MappedInputManager.h"
#include "ReaderPresetNames.h"
#include "ReaderPresetStore.h"
#include "activities/ActivityResult.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

size_t ReaderPresetsActivity::presetCount() const { return READER_PRESETS.getCount(); }

bool ReaderPresetsActivity::hasSaveRow() const { return !READER_PRESETS.isFull(); }

int ReaderPresetsActivity::rowCount() const { return static_cast<int>(presetCount()) + (hasSaveRow() ? 1 : 0); }

bool ReaderPresetsActivity::isSaveRow(const size_t index) const { return hasSaveRow() && index == presetCount(); }

int ReaderPresetsActivity::matchingPresetIndex() const {
  const auto& presets = READER_PRESETS.getPresets();
  for (size_t i = 0; i < presets.size(); i++) {
    // ReaderPrefs is trivially-copyable POD with no padding, so the whole blob compares
    // exactly — the same test the reader uses to decide a book has really changed.
    if (std::memcmp(&presets[i].prefs, &currentPrefs, sizeof(ReaderPrefs)) == 0) return static_cast<int>(i);
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

void ReaderPresetsActivity::openPresetActions(const size_t index) {
  const ReaderPreset* preset = READER_PRESETS.get(index);
  if (!preset) return;
  // Title by value: the popup keeps its own copy, and the store can change underneath.
  const std::string name = preset->name;
  const char* const options[] = {tr(STR_APPLY), tr(STR_RENAME), tr(STR_OVERWRITE_WITH_CURRENT), tr(STR_DELETE)};
  optionPopup.show(name.c_str(), options, 4, 0, [this, index](const int choice) {
    switch (choice) {
      case 0:
        // Only the reader can re-lay out the book, so this is the one action that leaves.
        setResult(PresetResult{index});
        finish();
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
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) {
    // The popup acts on button press; if that input closed it, the trailing release
    // must be swallowed below (Back would leave the screen, Confirm would reopen it).
    popupClosing = !optionPopup.isActive();
    return true;
  }
  if (popupClosing) {
    if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
        mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      return true;  // closing press still held
    }
    popupClosing = false;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      return true;  // swallow the release that closed the popup
    }
  }
  return false;
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
    labels[i] = isSaveRow(static_cast<size_t>(i)) ? std::string(tr(STR_SAVE_CURRENT_LOOK)) : presets[i].name;
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
