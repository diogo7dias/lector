#pragma once
#include <string>

#include "activities/Activity.h"

class Bitmap;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout = false)
      : Activity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout) {}
  void onEnter() override;

 private:
  // Everything onEnter() does apart from bookkeeping. Split out so onEnter() can wrap it
  // and record which wallpaper (if any) ended up on the panel; the render functions
  // return from several places.
  void renderSleepScreen() const;
  // The Lector fallback: white page, the name centred. Every face that finds nothing to
  // show (no wallpaper, no cover, a decode that failed) lands here.
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderCoverSleepScreen() const;
  // Full-screen wallpaper or cover: cleared page, the bitmap centred (fit or crop), the
  // cover filter applied, the grayscale pipeline when the image and the filter allow it.
  void renderBitmapSleepScreen(const Bitmap& bitmap) const;

  bool fromTimeout = false;
  // The wallpaper the previous sleep left on the panel. onEnter clears the shared
  // APP_STATE field before rendering (so a non-wallpaper face leaves it empty),
  // and the paused-rotation path needs the old value to know what to hold.
  mutable std::string previousWallpaper;
};
