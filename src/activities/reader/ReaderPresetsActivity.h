#pragma once
#include <cstddef>

#include "ReaderPrefs.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

// "Reading Themes": the saved reader looks, listed for the book the user is in.
// Sibling of StealLookActivity — that one copies a look from another book, this one
// from a named set on the card (ReaderPresetStore).
//
// The screen owns every edit: renaming, overwriting and deleting all happen here
// against the store. Only "Apply" goes back to the reader (as PresetResult, the index
// of the chosen theme), because only the reader can re-lay out the book. Back cancels.
class ReaderPresetsActivity final : public Activity {
 public:
  ReaderPresetsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const ReaderPrefs& currentPrefs)
      : Activity("ReaderPresets", renderer, mappedInput), currentPrefs(currentPrefs) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // This book's settings as they are right now: what "Save current look" stores, what
  // "Overwrite with current" writes, and what the Current mark is compared against.
  ReaderPrefs currentPrefs;

  size_t selectorIndex = 0;
  ButtonNavigator buttonNavigator;
  OptionPopup optionPopup;
  // True while the button press that closed the popup is still held; its release must
  // not fall through to this screen's own Back/Confirm handlers.
  bool popupClosing = false;

  // Rows are the saved themes, then "Save current look..." — which is absent once the
  // store is full, since there is nothing to save into.
  size_t presetCount() const;
  bool hasSaveRow() const;
  int rowCount() const;
  bool isSaveRow(size_t index) const;
  // Index of the theme whose settings equal this book's, or -1 when none does.
  int matchingPresetIndex() const;

  void activateSelected();
  void openPresetActions(size_t index);
  void startSaveCurrent();
  void startRename(size_t index);
  void confirmDelete(size_t index);
  // Keep the cursor on a row that still exists after an add or a delete.
  void clampSelector();
};
