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

  // X4 Pro UC8279 only: after the results, one step per PLL/TSSET pair, each left on
  // screen for the reader to judge, then a table of what each cost.
  struct SweepStep {
    uint8_t pll;  // written after PON; 0 = stock, no write
    uint8_t tsFast;
  };
  static constexpr SweepStep kSweep[] = {{0, 0x5A}, {0x0E, 0x5A}, {0x0F, 0x5A}, {0x0C, 0x5A},
                                         {0, 0x64}, {0, 0x78},    {0, 0x46}};
  static constexpr int kSweepSteps = sizeof(kSweep) / sizeof(kSweep[0]);
  static constexpr int kSweepTurns = 3;
  struct SweepResult {
    uint16_t totalMs = 0;
    uint16_t waveMs = 0;
    uint8_t promoted = 0;
  };
  SweepResult sweep[kSweepSteps];
  enum class Phase : uint8_t { Bench, Results, Sweep, Table };
  Phase phase = Phase::Bench;
  int step = 0;

  void drawTestPage(bool shifted, const char* header) const;
  Pass refresh(HalDisplay::RefreshMode mode) const;
  void runBench();
  void drawResults() const;
  static bool canSweep();
  void runSweepStep();
  void drawSweepTable() const;
};
