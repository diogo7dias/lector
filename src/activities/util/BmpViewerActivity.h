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
  void doSetSleepCover();
  // Sleep-folder triage, mirroring PxcViewerActivity so the two viewers agree.
  void doToggleFavorite();
  void doTogglePause();
  void promptDelete();

  std::string filePath;
  // Arena-backed and bounded; see NameList. A wallpaper folder with thousands of
  // images used to exhaust the heap building this list.
  NameList siblingImages;
  int currentImageIndex = -1;
};