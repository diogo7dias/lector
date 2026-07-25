#pragma once

#include "activities/Activity.h"
#include "components/OptionPopup.h"

// Removes the cache directories of books that are no longer on the card, leaving
// every present book's cache (and therefore its reading progress) untouched.
// This is the safe counterpart of ClearCacheActivity, which wipes ALL caches.
class CleanStorageActivity final : public Activity {
 public:
  explicit CleanStorageActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CleanStorage", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }  // Prevent power-saving mode
  void render(RenderLock&&) override;

 private:
  enum State { WARNING, CLEANING, SUCCESS, FAILED };

  State state = WARNING;

  void goBack() { finish(); }

  int removedCount = 0;
  int keptCount = 0;
  int failedCount = 0;
  OptionPopup confirmPopup;
  void beginClean();
  void cleanStorage();
};
