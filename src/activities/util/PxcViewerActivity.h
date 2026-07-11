#pragma once

#include <string>

#include "MappedInputManager.h"
#include "activities/Activity.h"

// Full-screen preview for a .pxc sleep wallpaper opened from the file browser,
// the counterpart to BmpViewerActivity for the packed format. Renders through
// the same PxcSleepRenderer the sleep screen uses; Back returns to the browser.
class PxcViewerActivity final : public Activity {
 public:
  // resultMode: when true (launched by the file browser via startActivityForResult),
  // Back/Move/Delete return an ActivityResult and finish() so the still-alive browser
  // can patch its in-memory list without a folder rescan, instead of rebuilding a
  // fresh browser via goToFileBrowser (the default, used by the ReaderActivity path).
  // The result is a FilePathResult: empty path = the file left this folder (moved or
  // deleted); non-empty = still present, at this (possibly favorite-renamed) path.
  PxcViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath,
                    bool resultMode = false);

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  void render();  // draw the wallpaper exactly like the lock screen
  // Returns to the browser: via a result+finish() in resultMode, else goToFileBrowser.
  // removed=true means the file left this folder (moved/deleted).
  void returnToBrowser(bool removed);
  std::string filePath;
  std::string launchPath;
  bool resultMode;
};
