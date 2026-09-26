#pragma once

#include <string>

#include "activities/UiStatusActivity.h"
#include "components/OptionPopup.h"

// Settings' two cache sweeps, one confirm-sweep-report flow. Clear Cache wipes ALL
// book caches. Clean Storage (orphansOnly) removes only the caches of books no longer
// on the card, leaving every present book's cache, and so its reading progress, alone.
class ClearCacheActivity final : public UiStatusActivity {
 public:
  explicit ClearCacheActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const bool orphansOnly = false)
      : UiStatusActivity(orphansOnly ? "CleanStorage" : "ClearCache", renderer, mappedInput),
        orphansOnly(orphansOnly) {}

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
  enum State { WARNING, CLEARING, SUCCESS, FAILED };

  const bool orphansOnly;
  State state = WARNING;

  void goBack() { finish(); }

  // The result line counts things, so it cannot be a translated constant; it is
  // built once when the sweep ends and handed to the view by pointer.
  std::string resultLine;
  OptionPopup confirmPopup;
  void beginClear();
  void clearCache();
  bool clearAllCaches(int& clearedCount, int& failedCount);
};
