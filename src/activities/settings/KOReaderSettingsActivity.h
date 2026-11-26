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
  // openRow: the row selected on entry (the one a reboot from its screen came from).
  explicit KOReaderSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int openRow = 0)
      : UiListActivity("KOReaderSettings", renderer, mappedInput), openRow(openRow) {}

  void onEnter() override;
  void onExit() override;
  // KOReader Auth reboots after Wi-Fi: the boot rebuilds this list on the row that opened it.
  bool restartStep(uint8_t& opener, int& row) const override;

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
  int openRow = 0;
};
