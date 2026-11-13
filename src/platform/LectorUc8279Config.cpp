// X4 Pro UC8279 config, selected with -DFREEINK_UC8279_X4_CONFIG=lectorUc8279X4Config on
// the x4pro environment: the SDK default, with Fast refreshes (page turns) on a host
// waveform instead of the panel's own DU.
//
// Measured on an X4 Pro UC8279 (Speed Test sweep, lector.exp.89): the OTP DU turns a page
// in 692 ms. A host pulse costs about 19 ms per frame plus about 165 ms per refresh; 12
// frames turn it in 520 ms and read as clean as the OTP DU over a chapter (exp.90), while
// 4 frames ghost.

#include <cstdint>

#include "driver/Uc8279X4Driver.h"

namespace freeink {

namespace {

// Calibration knob: frames of drive toward the target. Lower is faster; raise it if text
// washes out (cold panels move slower) or old pages ghost through.
#ifndef LECTOR_UC8279_PULSE_FRAMES
#define LECTOR_UC8279_PULSE_FRAMES 12
#endif
static_assert(LECTOR_UC8279_PULSE_FRAMES > 0 && LECTOR_UC8279_PULSE_FRAMES <= 0x3F, "pulse: 1..63 frames");
constexpr uint8_t kFrames = LECTOR_UC8279_PULSE_FRAMES;

// A waveform group is 7 bytes: six phase bytes (level << 6 | frames) and a repeat count
// LAST (a repeat-first table ran zero times on the panel, exp.88). Here: one grounded
// frame, the pulse, one grounded frame, once. Under the fast CDI (0xD7) 0x22 drives level
// 10 (white) and 0x23 level 01 (black), as the stock XTF_PRE_BW_MID tables do; VCOM (0x20)
// and the unchanged WW (0x21) and BB (0x24) cells idle at level 00.
uint8_t pulse[5][43];

Uc8279X4Config build() {
  static constexpr uint8_t kLevel[5] = {0x00, 0x00, 0x80, 0x40, 0x00};
  for (uint8_t i = 0; i < 5; i++) {
    pulse[i][0] = static_cast<uint8_t>(0x20 + i);  // LUT register
    pulse[i][1] = 0x01;
    pulse[i][2] = static_cast<uint8_t>(kLevel[i] | kFrames);
    pulse[i][6] = 0x01;
    pulse[i][7] = 0x01;  // repeat
  }
  Uc8279X4Config cfg = uc8279X4DefaultConfig();
  cfg.fastLut = pulse;
  return cfg;
}

}  // namespace

const Uc8279X4Config& lectorUc8279X4Config() {
  static const Uc8279X4Config cfg = build();
  return cfg;
}

}  // namespace freeink
