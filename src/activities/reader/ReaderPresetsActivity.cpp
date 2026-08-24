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
  const int rows = rowCount();
  if (rows <= 0) {
    selectorIndex = 0;
    return;
  }
  if (selectorIndex >= static_cast<size_t>(rows)) selectorIndex = static_cast<size_t>(rows - 1);
}

void ReaderPresetsActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;
  requestUpdate();
}

void ReaderPresetsActivity::onExit() { Activity::onExit(); }

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

void ReaderPresetsActivity::activateSelected() {
  if (isSaveRow(selectorIndex)) {
    startSaveCurrent();
    return;
  }
  if (selectorIndex >= presetCount()) return;
  openPresetActions(selectorIndex);
}

// ── Lifecycle ───────────────────────────────────────────────────────────────

void ReaderPresetsActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) {
    // The popup acts on button press; if that input closed it, the trailing release
    // must be swallowed below (Back would leave the screen, Confirm would reopen it).
    popupClosing = !optionPopup.isActive();
    return;
  }
  if (popupClosing) {
    if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
        mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      return;  // closing press still held
    }
    popupClosing = false;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      return;  // swallow the release that closed the popup
    }
  }

  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, true);

  // A tap on a row selects and activates it, the same as every other list.
  int tappedRow = 0;
  if (mappedInput.wasRowTapped(tappedRow) && tappedRow >= 0 && tappedRow < rowCount()) {
    selectorIndex = static_cast<size_t>(tappedRow);
    activateSelected();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    ActivityResult res;
    res.isCancelled = true;
    setResult(std::move(res));
    finish();
    return;
  }

  const int listSize = rowCount();

  buttonNavigator.onNextPress([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });
  buttonNavigator.onPreviousPress([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
}

void ReaderPresetsActivity::render(RenderLock&&) {
  // Drawn over the list still in the framebuffer, so the popup reads as sitting on it.
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_READING_THEMES));

  int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  // One status line above the list: nothing saved yet, or no slot left to save into.
  // The list itself is never empty — it always holds either themes or the save row.
  const char* note = nullptr;
  if (presetCount() == 0) {
    note = tr(STR_READING_THEMES_NONE);
  } else if (!hasSaveRow()) {
    note = tr(STR_READING_THEMES_FULL);
  }
  if (note != nullptr) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, note);
    const int noteHeight = 20 + renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;
    contentTop += noteHeight;
    contentHeight -= noteHeight;
  }

  const int matching = matchingPresetIndex();
  const auto& presets = READER_PRESETS.getPresets();

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, rowCount(), static_cast<int>(selectorIndex),
      [this, &presets](int index) {
        if (isSaveRow(static_cast<size_t>(index))) return std::string(tr(STR_SAVE_CURRENT_LOOK));
        return presets[index].name;
      },
      nullptr, nullptr,
      // The book's own settings are marked in the value column rather than in the name,
      // so the name stays what the user typed and the mark moves as the book changes.
      [matching](int index) { return index == matching ? std::string(tr(STR_CURRENT)) : std::string(); });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
