#pragma once
#include "activities/UiStatusActivity.h"

// What the reader shows after a panic: the sentence explaining what happened,
// then the reason the firmware captured. Both are prose of unknown length, so
// they are sections the base wraps rather than lines placed by hand.
class CrashActivity final : public UiStatusActivity {
  std::string panicMessage;

 public:
  explicit CrashActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiStatusActivity("Crash", renderer, mappedInput) {}
  void onEnter() override;

 protected:
  StatusView statusView() const override;
};
