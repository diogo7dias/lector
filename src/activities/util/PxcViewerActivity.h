#pragma once

#include <string>

#include "MappedInputManager.h"
#include "activities/Activity.h"

// Full-screen preview for a .pxc sleep wallpaper opened from the file browser,
// the counterpart to BmpViewerActivity for the packed format. This is the triage
// screen for a wallpaper folder: look at one, star it, retire it or delete it,
// then flick to the next.
//
// Up/Down step through the folder in name order without ever listing it — see
// WallpaperNeighbour.h for why a folder of thousands of images cannot be held in
// memory. That also rules out BmpViewerActivity's sibling-vector approach here.
class PxcViewerActivity final : public Activity {
 public:
  PxcViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath);

  void onEnter() override;
  void onExit() override;
  void loop() override;

  // Public only so the file-local overlay trampoline can reach it; not part of
  // the activity's interface. See the trampoline in the .cpp for why the hook
  // cannot take a context pointer.
  void drawHintsForOverlay() const;

 private:
  void render();
  // Steps to the next (+1) or previous (-1) wallpaper in the folder. No-op at the
  // ends of the folder.
  void openNeighbour(int delta);
  // Draws the button hints. Called through a trampoline as renderPxcSleepScreen's
  // overlay hook, so the hints composite into the same refresh as the wallpaper
  // instead of costing a second one.
  void drawHints() const;

  std::string filePath;
  // Set to false once the file has been deleted, so onExit does not try to keep
  // showing it.
  bool fileStillPresent = true;
};
