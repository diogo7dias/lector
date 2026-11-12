#pragma once

#include "activities/UiStatusActivity.h"

// X4 Pro only: the Fast Page Turns switch and how to judge it. The waveform has never been
// measured on this panel, so the screen tells the reader what to look for and report.
class FastPageTurnsTestActivity final : public UiStatusActivity {
 public:
  explicit FastPageTurnsTestActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiStatusActivity("FastPageTurnsTest", renderer, mappedInput) {}

 protected:
  StatusView statusView() const override;
  void onConfirmButton() override;
};
