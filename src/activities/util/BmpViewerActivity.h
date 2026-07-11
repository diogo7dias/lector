#pragma once

#include <functional>
#include <string>

#include "MappedInputManager.h"
#include "activities/Activity.h"

class BmpViewerActivity final : public Activity {
 public:
  BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath,
                    bool resultMode = false);

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  void loadSiblingImages();
  void doSetSleepCover();
  void moveSleepPause();  // move the viewed wallpaper between /sleep and /sleep pause
  void returnToBrowser(bool removed);

  std::string filePath;
  std::vector<std::string> siblingImages;
  int currentImageIndex = -1;
  bool resultMode;
};
