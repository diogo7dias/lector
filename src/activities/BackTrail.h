#pragma once

#include <algorithm>
#include <cstdint>

// Where Back lands, packed into one RTC word so it rides a silent restart (SilentRestart.cpp).
// A radio screen that reboots on the way out to give its heap back would otherwise land Home.
// Bytes 0-2: the last three openers (ActivityManager::Opener), the deepest first, each as
// (opener + 1) << 4 | row; a zero byte ends the chain. Byte 3: the Home row (HomeMenuItem) for
// when the chain is empty. The row tops out at 15 and a deeper chain keeps only its last three
// openers: both are far past any real one.
namespace back_trail {

constexpr int kMaxOpeners = 3;
constexpr int kMaxRow = 15;

struct Step {
  uint8_t opener = 0;
  uint8_t row = 0;
};

struct Trail {
  Step steps[kMaxOpeners];
  int count = 0;
  uint8_t homeRow = 0;
};

// steps[0] is the deepest opener; only the last kMaxOpeners are kept.
inline uint32_t pack(const Step* steps, const int count, const uint8_t homeRow) {
  uint32_t word = static_cast<uint32_t>(homeRow) << 24;
  const int kept = std::min(count, kMaxOpeners);
  for (int i = 0; i < kept; ++i) {
    const Step& step = steps[count - kept + i];
    const uint32_t row = std::min<uint32_t>(step.row, kMaxRow);
    word |= ((static_cast<uint32_t>(step.opener) + 1) << 4 | row) << (8 * i);
  }
  return word;
}

// maxOpener and maxHomeRow bound what a stray word can name: an opener past maxOpener ends the
// chain there, a Home row past maxHomeRow reads as 0.
inline Trail unpack(const uint32_t word, const uint8_t maxOpener, const uint8_t maxHomeRow) {
  Trail trail;
  for (int i = 0; i < kMaxOpeners; ++i) {
    const uint8_t byte = static_cast<uint8_t>(word >> (8 * i));
    const int opener = (byte >> 4) - 1;
    if (opener < 0 || opener > maxOpener) break;
    trail.steps[trail.count++] = Step{static_cast<uint8_t>(opener), static_cast<uint8_t>(byte & 0x0F)};
  }
  const uint8_t home = static_cast<uint8_t>(word >> 24);
  trail.homeRow = home <= maxHomeRow ? home : 0;
  return trail;
}

}  // namespace back_trail
