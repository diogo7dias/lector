#pragma once

#include <functional>
#include <string>

#include "activities/UiStatusActivity.h"
#include "components/OptionPopup.h"

class ClearCacheActivity final : public UiStatusActivity {
 public:
  explicit ClearCacheActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiStatusActivity("ClearCache", renderer, mappedInput) {}

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

  State state = WARNING;

  void goBack() { finish(); }

  int clearedCount = 0;
  int failedCount = 0;
  // The result line counts things, so it cannot be a translated constant; it is
  // built once when the sweep ends and handed to the view by pointer.
  std::string resultLine;
  OptionPopup confirmPopup;
  void beginClear();
  void clearCache();
};
