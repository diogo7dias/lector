#pragma once

#include <string>

class GfxRenderer;

// Draws the optional sleep-screen info overlay in the bottom-left corner:
//  - showSleepImageFilename ON  -> the wallpaper's (favorite-suffix-stripped)
//    filename in a filled box.
//  - else showSleepFavoriteBadge ON and the file is a favorite -> a small "F" box.
// No-op when neither applies or no wallpaper is in scope.
//
// Signature matches renderPxcSleepScreen's overlay hook, and it must be called
// once per render pass (BW base + LSB + MSB) so it composites solid, mirroring
// how the wallpaper itself is redrawn per pass.
void drawSleepInfoOverlay(GfxRenderer& renderer);

// Names the wallpaper the overlay describes, for as long as the guard lives.
// The overlay hook is a plain function pointer with no context parameter, so the
// path has to reach drawSleepInfoOverlay out of band. Scoping it to a guard
// means it cannot outlive the render that set it and leak a stale filename onto
// a later screen. Not reentrant: one sleep render is in flight at a time.
class SleepInfoOverlayScope {
 public:
  explicit SleepInfoOverlayScope(const std::string& sourcePath);
  ~SleepInfoOverlayScope();

  SleepInfoOverlayScope(const SleepInfoOverlayScope&) = delete;
  SleepInfoOverlayScope& operator=(const SleepInfoOverlayScope&) = delete;
};
