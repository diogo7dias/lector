#pragma once

// Auto-repeat ramp for held-button value editing.
//
// A numeric setting has to serve two gestures with the same button: a nudge of one, and a
// run across the whole range. Line spacing spans 35..150, so a flat one-per-repeat hold at
// the 120 ms edit interval costs about fourteen seconds end to end. Stepping by five from
// the start fixes that but makes the odd values unreachable while held.
//
// So the hold ramps: fine while the press still reads as a nudge, coarse once it plainly
// does not. The caller passes how many repeats this hold has already fired and resets its
// counter on release, so every hold earns the ramp again from zero.

// Repeats before the step goes coarse. At the 120 ms repeat interval this is about one
// second of holding, on top of the 350 ms the repeat itself waits before starting.
constexpr unsigned HOLD_REPEAT_COARSE_AFTER = 8;

// Size of one coarse step. Five divides every range these settings use and keeps the
// round numbers (percentages, margins) landable while the ramp is running.
constexpr int HOLD_REPEAT_COARSE_STEP = 5;

// Step size for the repeat at `repeatIndex` (0 = the first repeat of this hold).
[[nodiscard]] constexpr int holdRepeatStep(const unsigned repeatIndex) {
  return repeatIndex < HOLD_REPEAT_COARSE_AFTER ? 1 : HOLD_REPEAT_COARSE_STEP;
}
