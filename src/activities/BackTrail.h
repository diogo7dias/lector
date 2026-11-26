#pragma once

#include <algorithm>
#include <cstdint>

// Where Back lands, packed into two RTC words so it rides a silent restart (SilentRestart.cpp).
// A radio screen that reboots on the way out to give its heap back would otherwise land Home.
// Bits 0-47: the last three openers (ActivityManager::Opener), the deepest first, 16 bits each:
// (opener + 1) << 12 | row; a zero step ends the chain. Bits 56-63: the Home row (HomeMenuItem)
// for when the chain is empty. The row tops out at 4095 and a deeper chain keeps only its last
// three openers: both are far past any real one.
namespace back_trail {

constexpr int kMaxOpeners = 3;
constexpr int kMaxRow = 0x0FFF;

struct Step {
  uint8_t opener = 0;
  uint16_t row = 0;
};

struct Trail {
  Step steps[kMaxOpeners];
  int count = 0;
  uint8_t homeRow = 0;
};

// steps[0] is the deepest opener; only the last kMaxOpeners are kept.
inline uint64_t pack(const Step* steps, const int count, const uint8_t homeRow) {
  uint64_t word = static_cast<uint64_t>(homeRow) << 56;
  const int kept = std::min(count, kMaxOpeners);
  for (int i = 0; i < kept; ++i) {
    const Step& step = steps[count - kept + i];
    const uint64_t row = std::min<uint64_t>(step.row, kMaxRow);
    word |= ((static_cast<uint64_t>(step.opener) + 1) << 12 | row) << (16 * i);
  }
  return word;
}

// maxOpener and maxHomeRow bound what a stray word can name: an opener past maxOpener ends the
// chain there, a Home row past maxHomeRow reads as 0.
inline Trail unpack(const uint64_t word, const uint8_t maxOpener, const uint8_t maxHomeRow) {
  Trail trail;
  for (int i = 0; i < kMaxOpeners; ++i) {
    const auto step = static_cast<uint16_t>(word >> (16 * i));
    const int opener = (step >> 12) - 1;
    if (opener < 0 || opener > maxOpener) break;
    trail.steps[trail.count++] = Step{static_cast<uint8_t>(opener), static_cast<uint16_t>(step & kMaxRow)};
  }
  const auto home = static_cast<uint8_t>(word >> 56);
  trail.homeRow = home <= maxHomeRow ? home : 0;
  return trail;
}

}  // namespace back_trail
