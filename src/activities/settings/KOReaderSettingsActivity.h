#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

/**
 * Submenu for KOReader Sync settings.
 * Shows username, password, and authenticate options.
 */
class KOReaderSettingsActivity final : public UiListActivity {
 public:
  explicit KOReaderSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("KOReaderSettings", renderer, mappedInput) {}

  void onExit() override;

 protected:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

 private:
  // The right-hand column for a row: what the setting currently is.
  std::string statusFor(int index) const;

  // Row values own their strings; the ListItems borrow them.
  std::vector<std::string> values;
  std::vector<freeink::ui::ListItem> rows;
};
