#pragma once

#include <string>

#include "MappedInputManager.h"
#include "activities/Activity.h"

// Full-screen preview for a .pxc sleep wallpaper opened from the file browser,
// the counterpart to BmpViewerActivity for the packed format. Renders through
// the same PxcSleepRenderer the sleep screen uses; Back returns to the browser.
class PxcViewerActivity final : public Activity {
 public:
  PxcViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath);

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  std::string filePath;
};
