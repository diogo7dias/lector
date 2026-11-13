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
}  // namespace freeink
