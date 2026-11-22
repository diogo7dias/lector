#pragma once

#include <NameList.h>

#include <functional>
#include <string>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"

class BmpViewerActivity final : public Activity {
 public:
  BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath);

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  void loadSiblingImages();
  // Draws the button hint strip for the current file. Shared by the full render and
  // by the hint-only repaint, so the two can never disagree about the labels.
  void drawHints();
  void doSetSleepCover();
  // Sleep-folder triage; the work itself lives in SleepImageTriage, shared with PxcViewerActivity.
  void doToggleFavorite();
  void doTogglePause();
  void promptDelete();
  // A .bmp or .jpg can become the wallpaper (a .jpg is converted); a .png or .bmp can
  // become the overlay while a Transparent face is selected. Anything else would write a
  // file nothing ever renders.
  bool canSetSleepCover() const;
  // A .png or .jpg onto the framebuffer.
  bool renderDecoded();
  // Touch: the outer thirds step through the folder, the centre opens the actions the
  // front keys run here. Returns true when the pass was spent.
  bool handleTouch();
  // The image delta places along the folder; false at either end.
  bool openSibling(int delta);
  void openActionMenu();
  bool handleActionMenu();

  std::string filePath;
  // Arena-backed and bounded; see NameList. A wallpaper folder with thousands of
  // images used to exhaust the heap building this list.
  NameList siblingImages;
  int currentImageIndex = -1;

  // The centre-tap menu: its rows are the key actions for this folder, in key order.
  enum class MenuAction : uint8_t { SetSleepCover, Favorite, Delete, Pause };
  OptionPopup actionMenu;
  MenuAction menuActions[3] = {};
  int menuChoice = -1;
  bool menuDirty = false;
};
