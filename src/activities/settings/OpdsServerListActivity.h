#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

/**
 * Activity showing the list of configured OPDS servers.
 * Allows adding new servers and editing/deleting existing ones.
 * When pickerMode is true, selecting a server navigates to the OPDS browser
 * instead of opening the editor (used from the home screen).
 */
class OpdsServerListActivity final : public UiListActivity {
 public:
  // openRow: the row selected on entry (the server a Back from its browser came from).
  explicit OpdsServerListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool pickerMode = false,
                                  int openRow = 0)
      : UiListActivity(activity_name::kOpdsServerList, renderer, mappedInput),
        pickerMode(pickerMode),
        openRow(openRow) {}

  void onEnter() override;
  void onExit() override;

 protected:
  int listCount() const override { return getItemCount(); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  const char* headerTitle() const override;

 private:
  bool pickerMode = false;
  int openRow = 0;

  int getItemCount() const;

  // Row text owns its strings; the ListItems borrow them.
  std::vector<std::string> labels;
  std::vector<std::string> subtitles;
  std::vector<freeink::ui::ListItem> rows;
};
