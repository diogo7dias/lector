#pragma once

#include <NameList.h>

#include <functional>
#include <string>

#include "MappedInputManager.h"
#include "activities/Activity.h"

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

  std::string filePath;
  // Arena-backed and bounded; see NameList. A wallpaper folder with thousands of
  // images used to exhaust the heap building this list.
  NameList siblingImages;
  int currentImageIndex = -1;
};
