#pragma once

#include <atomic>
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

  // X4 Pro UC8279 only: after the results, one step per Fast waveform, each left on
  // screen for the reader to judge, then a table of what each cost.
  // wave: 0 the panel's own DU, 1 the stock X3 DU bank, 2 one pulse of `frames`.
  struct SweepStep {
    uint8_t wave;
    uint8_t frames;
  };
  static constexpr SweepStep kSweep[] = {{0, 0}, {1, 0}, {2, 4}, {2, 8}, {2, 12}, {2, 16}, {2, 24}};
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
  // Set by loop() when it queues a step, cleared when render() finishes it: a key pressed
  // meanwhile would otherwise skip a step (exp.88 lost P10 that way).
  std::atomic<bool> busy{false};

  void drawTestPage(bool shifted, const char* header) const;
  Pass refresh(HalDisplay::RefreshMode mode) const;
  void runBench();
  void drawResults() const;
  static bool canSweep();
  static const char* waveName(const SweepStep& s);
  void runSweepStep();
  void drawSweepTable() const;
};
