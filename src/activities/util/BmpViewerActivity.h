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
  // Sleep-folder triage, mirroring PxcViewerActivity so the two viewers agree.
  void doToggleFavorite();
  // The path this file WILL have once the favorite queue drains, or filePath itself when
  // nothing is queued for it. The hint strip reads through here so a wallpaper the user
  // has just favorited does not still offer "Favorite" while the rename waits.
  std::string effectivePath() const;
  bool effectiveFavorite() const;
  void doTogglePause();
  void promptDelete();
  // A PNG can only become the sleep image while the Transparent face is selected;
  // every other face reads .bmp only, so offering it there would write a file
  // nothing ever renders.
  bool canSetSleepCover() const;
  bool renderPng();

  std::string filePath;
  // Arena-backed and bounded; see NameList. A wallpaper folder with thousands of
  // images used to exhaust the heap building this list.
  NameList siblingImages;
  int currentImageIndex = -1;
};
