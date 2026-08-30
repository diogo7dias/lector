#pragma once

#include <string>

#include "activities/UiStatusActivity.h"
#include "components/OptionPopup.h"

// Removes the cache directories of books that are no longer on the card, leaving
// every present book's cache (and therefore its reading progress) untouched.
// This is the safe counterpart of ClearCacheActivity, which wipes ALL caches.
class CleanStorageActivity final : public UiStatusActivity {
 public:
  explicit CleanStorageActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiStatusActivity("CleanStorage", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  bool skipLoopDelay() override { return true; }  // Prevent power-saving mode

 protected:
  StatusView statusView() const override;
  bool handleCustomInput() override;
  bool drawOverlay() override;
  void onConfirmButton() override;
  void onBackButton() override;

 private:
  enum State { WARNING, CLEANING, SUCCESS, FAILED };

  State state = WARNING;

  void goBack() { finish(); }

  int removedCount = 0;
  int keptCount = 0;
  int failedCount = 0;
  // Counted, so it is built when the sweep ends rather than translated.
  std::string resultLine;
  OptionPopup confirmPopup;
  void beginClean();
  void cleanStorage();
};
