// X4 Pro UC8279 config, selected with -DFREEINK_UC8279_X4_CONFIG=lectorUc8279X4Config
// in Speed Kit builds only. It is the SDK default, held mutable so the Speed Test sweep
// can swap the Fast waveform between refreshes: the driver keeps a reference and reads
// fastLut on every refresh.

#include "LectorUc8279Config.h"

#include <cstring>

#include "lut/Uc8279X3Luts.h"

namespace freeink {

namespace {
Uc8279X4Config& tune() {
  static Uc8279X4Config cfg = uc8279X4DefaultConfig();
  return cfg;
}

// A waveform group is 7 bytes, phase bytes (level << 6 | frames) and one repeat byte.
// exp.88 put the repeat first and the panel skipped the group, so the repeat is last;
// the pulse below reads as one frame, n frames, one frame either way round. Under the
// fast CDI (0xD7), 0x22 drives level 10 (white) and
// 0x23 level 01 (black), as the stock pre-pass tables do; 0x20 VCOM, 0x21 WW and
// 0x24 BB idle at level 00 for the same frame count.
uint8_t pulse[5][43];
}  // namespace

const Uc8279X4Config& lectorUc8279X4Config() { return tune(); }

void lectorUc8279SetFastWave(const Uc8279FastWave wave, const uint8_t frames) {
  switch (wave) {
    case Uc8279FastWave::Otp:
      tune().fastLut = nullptr;
      return;
    case Uc8279FastWave::X3Du:
      tune().fastLut = kUc8279X3_BwDu;
      return;
    case Uc8279FastWave::Pulse: {
      const uint8_t n = frames & 0x3F;
      static constexpr uint8_t kLevel[5] = {0x00, 0x00, 0x80, 0x40, 0x00};
      memset(pulse, 0, sizeof(pulse));
      for (uint8_t i = 0; i < 5; i++) {
        pulse[i][0] = static_cast<uint8_t>(0x20 + i);
        pulse[i][1] = 0x01;  // one grounded frame
        pulse[i][2] = static_cast<uint8_t>(kLevel[i] | n);
        pulse[i][6] = 0x01;  // one grounded frame
        pulse[i][7] = 0x01;  // repeat once
      }
      tune().fastLut = pulse;
      return;
    }
  }
}

}  // namespace freeink
