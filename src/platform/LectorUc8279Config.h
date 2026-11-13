#pragma once

#include <cstdint>

#include "driver/Uc8279X4Driver.h"

namespace freeink {
// The live X4 Pro UC8279 config. Only takes effect where the build injects it
// (FREEINK_UC8279_X4_CONFIG); change it only between refreshes.
const Uc8279X4Config& lectorUc8279X4Config();

// Speed Test sweep: which waveform a Fast refresh runs.
enum class Uc8279FastWave : uint8_t {
  Otp,    // the panel's own DU (stock)
  X3Du,   // stock X3 UC8279d DU bank, uploaded
  Pulse,  // one pulse of `frames` toward the target, changed pixels only
};
void lectorUc8279SetFastWave(Uc8279FastWave wave, uint8_t frames = 0);

// Page turns run the pulse at this length. exp.89 on the X4 Pro: 19 ms per frame plus
// about 165 ms per refresh; P12 turned a page in 520 ms (stock 692) as clean as stock,
// P4 ghosted. Calibration knob: lower for speed, raise if text washes out when cold.
constexpr uint8_t kReadingPulseFrames = 12;
}  // namespace freeink
