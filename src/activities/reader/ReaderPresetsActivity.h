#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "ReaderPrefs.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// "Reading Themes": the saved reader looks, listed for the book the user is in.
// Sibling of StealLookActivity — that one copies a look from another book, this one
// from a named set on the card (ReaderPresetStore).
//
// The screen owns every edit: renaming, overwriting and deleting all happen here
// against the store. Only "Apply" goes back to the reader (as PresetResult, the index
// of the chosen theme), because only the reader can re-lay out the book. Back cancels.
class ReaderPresetsActivity final : public UiListActivity {
 public:
  ReaderPresetsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const ReaderPrefs& currentPrefs,
                        bool inBook = true)
      : UiListActivity("ReaderPresets", renderer, mappedInput), currentPrefs(currentPrefs), inBook_(inBook) {}

  void onExit() override;

 protected:
  int listCount() const override { return rowCount(); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  const char* headerTitle() const override;
  ListChrome chrome() const override;
  // The theme actions popup owns the pass while it is up, and paints over the
  // finished list.
  bool handleCustomInput() override;
  bool drawOverlay() override;

 private:
  // This book's settings (or global defaults when opened from Settings > Reader): what
  // "Save current look" stores, what "Overwrite with current" writes, and what the Current
  // mark is compared against.
  ReaderPrefs currentPrefs;
  bool inBook_ = true;

  OptionPopup optionPopup;

  // Rows: saved themes, optional "Save current look...", optional "Send Themes via Nearby".
  size_t presetCount() const;
  bool hasSaveRow() const;
  bool hasSendRow() const;
  int rowCount() const;
  bool isSaveRow(size_t index) const;
  bool isSendRow(size_t index) const;
  // Index of the theme whose settings equal currentPrefs, or -1 when none does.
  int matchingPresetIndex() const;

  void openPresetActions(size_t index);
  void applyAsGlobalDefault(size_t index);
  void startSendNearby();
  void startSaveCurrent();
  void startRename(size_t index);
  void confirmDelete(size_t index);
  // Keep the cursor on a row that still exists after an add or a delete.
  void clampSelector();
  // The status line above the list, or nullptr when there is nothing to say.
  const char* noteText() const;

  // Row labels own their strings; the ListItems borrow them.
  std::vector<std::string> labels;
  std::vector<freeink::ui::ListItem> rows;
};
