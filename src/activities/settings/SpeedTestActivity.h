#pragma once

#include <cstdint>

#include "activities/Activity.h"

// Speed Kit builds only: one tap times the panel and the drawing so a reader can photo
// the result. Ten page turns, one clean, one full, and the CPU time to draw a page.
class SpeedTestActivity final : public Activity {
 public:
  explicit SpeedTestActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SpeedTest", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int kTurns = 10;
  struct Pass {
    uint32_t totalUs = 0;
    uint32_t wireUs = 0;
    uint32_t waveUs = 0;
    uint8_t mode = 0;
  };
  Pass turns[kTurns];
  Pass clean;
  Pass full;
  uint32_t drawUs = 0;
  bool done = false;

  void drawTestPage(bool shifted) const;
  Pass refresh(HalDisplay::RefreshMode mode) const;
  void runBench();
  void drawResults() const;
};
