#pragma once

#include <string>
#include <vector>

#include "OpdsServerStore.h"
#include "activities/UiListActivity.h"

/**
 * Edit screen for a single OPDS server.
 * Shows Name, URL, Username, Password fields and a Delete option.
 * Used for both adding new servers and editing existing ones.
 */
class OpdsSettingsActivity final : public UiListActivity {
 public:
  /**
   * @param serverIndex Index into OpdsServerStore, or -1 for a new server
   */
  explicit OpdsSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int serverIndex = -1)
      : UiListActivity("OpdsSettings", renderer, mappedInput), serverIndex(serverIndex) {}

  void onEnter() override;
  void onExit() override;

 protected:
  int listCount() const override { return getMenuItemCount(); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  // The header carries a hint line under it, which the base chrome does not offer.
  void drawChrome() override;
  bool drawOverlay() override;

 private:
  int serverIndex;
  OpdsServer editServer;
  bool isNewServer = false;
  bool showSaveError = false;

  int getMenuItemCount() const;
  bool saveServer();

  // Row text owns its strings; the ListItems borrow them.
  std::vector<std::string> subtitles;
  std::vector<freeink::ui::ListItem> rows;
};
