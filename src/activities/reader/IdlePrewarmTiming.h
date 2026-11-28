#pragma once

namespace idle_prewarm {

// Quiet time after the last user input before idle background work (neighbour
// glyph prewarm, debounced progress save, chapter build/prefetch) runs. Must
// stay below HalPowerManager::IDLE_POWER_SAVING_MS (1000 ms on X4 Pro) so
// prewarm finishes at full clock instead of bouncing 40 -> 240 -> 40 MHz on
// every page turn.
inline constexpr unsigned long DELAY_AFTER_INPUT_MS = 650;

// Ready once the current page has finished rendering and at least delayMs have
// elapsed since the last input. If the render itself ran longer than delayMs
// (e.g. a periodic clean refresh), this becomes true on the first loop pass
// after render completes, still before main.cpp drops the CPU clock.
constexpr bool isReady(const unsigned long nowMs, const unsigned long lastInputMs,
                       const unsigned long lastRenderCompleteMs, const unsigned long delayMs = DELAY_AFTER_INPUT_MS) {
  if (lastRenderCompleteMs == 0) return false;
  return (nowMs - lastInputMs) >= delayMs;
}

}  // namespace idle_prewarm
