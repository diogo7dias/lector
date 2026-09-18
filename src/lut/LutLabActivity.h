#pragma once

#include "activities/UiListActivity.h"

// Temporary release UI. No directory-sized allocation: retain only the current
// filename and one neighbour while browsing, even for thousands of wallpapers.
class LutLabActivity final : public UiListActivity {
 public:
  LutLabActivity(GfxRenderer& renderer, MappedInputManager& input) : UiListActivity("LutLab", renderer, input) {}
  void onEnter() override;

 private:
  void onBackButton() override;
  int listCount() const override { return 6; }
  ListChrome chrome() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void stepWallpaper(int delta);
  bool dirty = false;
  bool saveFailed = false;
  // Member scratch keeps FAT's maximum filename off the small task stack.
  char filename[256]{};
  mutable char variantLabel[96]{};
  mutable char bytesLabel[96]{};
  freeink::ui::ListItem items[6]{};
};
