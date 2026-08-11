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
  // Blank FULL pass before painting a sleep face, so the screen the user locked from
  // does not ghost through it. See the definition for why nothing else provides it.
  // Keeps the page already on the panel and adds a thin border. Must not be preceded
  // by the popup or the deep clean.
  void deepCleanPanel() const;
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderCoverSleepScreen() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap) const;
  // Reading-stats dashboard over the current book's cover. Falls back to the default
  // face when there is no open book, the format has no stats, or no cover can be made.
  void renderStatsDashboardSleepScreen() const;
  void renderLastScreenSleepScreen() const;
  void renderBlankSleepScreen() const;

  bool fromTimeout = false;
  // The wallpaper the previous sleep left on the panel. onEnter clears the shared
  // APP_STATE field before rendering (so a non-wallpaper face leaves it empty),
  // and the paused-rotation path needs the old value to know what to hold.
  mutable std::string previousWallpaper;
  // True when the render path changed APP_STATE beyond lastSleepWallpaperPath
  // (rotation cursor advanced, index flagged stale). onEnter folds it into the
  // single batched saveToFile before deep sleep.
  mutable bool stateDirty = false;
};
