#pragma once

#include "activities/Activity.h"

// "Clean Up Storage": removes cache directories for books that are no longer on
// the card (orphans), preserving every present book's reading progress, images,
// and thumbnails. Distinct from ClearCacheActivity, which wipes ALL caches.
class CleanStorageActivity final : public Activity {
 public:
  explicit CleanStorageActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CleanStorage", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }  // Prevent power-saving mode during the scan
  void render(RenderLock&&) override;

 private:
  enum State { WARNING, CLEANING, SUCCESS, FAILED };

  State state = WARNING;

  void goBack() { finish(); }

  int removedCount = 0;
  int keptCount = 0;
  int failedCount = 0;
  void cleanStorage();
};
