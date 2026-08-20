#pragma once
#include <string>

#include "activities/Activity.h"
#include "components/OptionPopup.h"

// The one-shot "battery low" warning, pushed over whatever is on screen when the charge
// first drops to the warning threshold. It carries a single option so any button dismisses
// it, and it is pushed rather than replacing the current screen, so a reading session
// survives the interruption.
//
// It also owns the "already warned" latch: the flag is written from onEnter(), the moment
// the notice is genuinely on screen. Written at the push site instead, a screen that
// replaced the pending activity before the manager honoured the push would swallow the
// warning while the flag on disk claimed it had been shown.
class LowBatteryNoticeActivity final : public Activity {
 public:
  LowBatteryNoticeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, uint16_t percent,
                           bool overReadingSurface);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Follows the surface it was pushed over: night mode inverts the reading surfaces only,
  // so a notice over a book must invert with it or the whole panel flips polarity around
  // it, twice, on a display that shows every flip.
  bool appliesNightMode() const override { return overReadingSurface; }

 private:
  uint16_t percent;
  bool overReadingSurface;
  std::string heading;
  OptionPopup noticePopup;
};
