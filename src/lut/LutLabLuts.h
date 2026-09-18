#pragma once

#include <array>
#include <cstdint>

namespace lutlab {
// Temporary calibration instrument, not a proposed replacement waveform.
// Snapshot of SDK lut_grayscale: 105 waveform bytes + 5 voltages + 2 reserved.
// The host check compares every byte against the SDK so the control cannot drift.
using Lut = std::array<unsigned char, 112>;
static constexpr Lut CONTROL = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x54, 0x54, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xAA, 0xA0, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA2, 0x22, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x01,
    0x01, 0x01, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x8F, 0x8F, 0x8F, 0x8F, 0x8F, 0x17, 0x41, 0xA8, 0x32, 0x30, 0x00, 0x00,
};

constexpr Lut change(unsigned index, unsigned char value) {
  Lut lut = CONTROL;
  lut[index] = value;
  return lut;
}

static constexpr std::array<Lut, 9> VARIANTS = {{
    CONTROL,            // 0: unchanged shipping control, including the voltage tail.
    change(109, 0x2F),  // 1: VCOM one step below stock; documented first calibration lever.
    change(109, 0x31),  // 2: VCOM one step above stock; direction must be judged on the panel.
    change(109, 0x2E),  // 3: second downward step, after comparing the immediate neighbours.
    change(109, 0x32),  // 4: second upward step; bounded symmetric VCOM sweep, no other change.
    change(106, 0x40),  // 5: VSH1 one step below stock, VCOM reset for attribution.
    change(106, 0x42),  // 6: VSH1 one step above stock; second documented calibration lever.
    change(60, 0x02),   // 7: first subphase of group 2, 1 -> 2 frames, stock voltages.
    change(61, 0x02),   // 8: second subphase of group 2, 1 -> 2 frames, stock voltages.
}};
// Group 2 is deliberately chosen: light gray's VS byte 12 is 0x40 and gray's
// byte 22 is 0xA8, so those tones use different source selections here. Timing
// is GLOBAL (also affects dark gray's 0x20); these are bigger-hammer probes,
// not a claim that either direction improves contrast. No VS polarity, repeat
// count, frame-rate, VGH, VSH2 or VSL changes; never combine a timing and voltage probe.
constexpr uint8_t validVariant(int value) {
  return value >= 0 && value < static_cast<int>(VARIANTS.size()) ? value : 0;
}
constexpr uint8_t stepVariant(uint8_t value, int delta) {
  return (validVariant(value) + VARIANTS.size() + delta) % VARIANTS.size();
}
}  // namespace lutlab
