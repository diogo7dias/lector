#pragma once

#include <array>
#include <cstdint>

namespace lutlab {
// UC8279 X4 AA: five consecutive 49-byte payloads for commands 0x20..0x24.
// Snapshot of kXtfAa02; the host bus test checks all 245 bytes against the SDK.
// Command bytes are NOT part of this buffer. All variants live in flash.
static constexpr unsigned TABLE_BYTES = 49;
using Lut = std::array<unsigned char, 5 * TABLE_BYTES>;
constexpr Lut control() {
  Lut lut{};
  for (unsigned table = 0; table < 5; ++table) {
    const unsigned start = table * TABLE_BYTES;
    lut[start] = 0x01;
    lut[start + 1] = 0x02;
    lut[start + 2] = (table == 2 || table == 3) ? 0x82 : 0x02;
    lut[start + 3] = table == 1 ? 0x41 : table == 4 ? 0x81 : 0x01;
    lut[start + 4] = lut[start + 5] = lut[start + 6] = 0x01;
    lut[start + 12] = lut[start + 13] = 0x01;
  }
  return lut;
}
static constexpr Lut CONTROL = control();

constexpr Lut grayFrames(uint8_t frames) {
  Lut lut = CONTROL;
  // ONE axis: the gray-phase duration. Step all five tables together, just as
  // the vendor does from Aa02 to Aa68, preserving the dark-gray polarity flag.
  // Splitting BW/WB from VCOM/WW/BB would test an unproven phase mismatch too.
  for (unsigned table = 0; table < 5; ++table) {
    lut[table * TABLE_BYTES + 2] = (lut[table * TABLE_BYTES + 2] & 0xF0) | frames;
  }
  return lut;
}

static constexpr std::array<Lut, 8> VARIANTS = {{
    CONTROL,        // 0: native stock (sleep passes nullptr; Aa68 stock is 3 frames).
    grayFrames(1),  // 1: one frame below Aa02, to compare direction.
    grayFrames(3),  // 2: byte-exact vendor Aa68 waveform.
    grayFrames(4),  // 3..7: successive one-frame probes, bounded at 8 (4x Aa02).
    grayFrames(5),
    grayFrames(6),
    grayFrames(7),
    grayFrames(8),
}};
// No voltage, polarity, PLL, repeat-count or preBW transition changes.
constexpr uint8_t validVariant(int value) {
  return value >= 0 && value < static_cast<int>(VARIANTS.size()) ? value : 0;
}
constexpr uint8_t stepVariant(uint8_t value, int delta) {
  return (validVariant(value) + VARIANTS.size() + delta) % VARIANTS.size();
}
}  // namespace lutlab
