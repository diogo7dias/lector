#include "WakeTiming.h"

#include <Arduino.h>

#include <cstdio>
#include <cstring>

namespace WakeTiming {
namespace {

constexpr uint8_t kCount = static_cast<uint8_t>(Stage::Count);

// A stage that never ran. Distinct from 0, which is a legitimate stamp very early in boot.
constexpr uint16_t kUnset = 0xFFFF;

// RTC_NOINIT survives deep sleep and is deliberately not zeroed at boot, which is exactly
// what carrying a measurement across a sleep needs. It IS uninitialised on a cold boot,
// so a magic word guards the contents; without it, flash noise would print as timings.
constexpr uint32_t kMagic = 0x57414B31;  // "WAK1"

RTC_NOINIT_ATTR uint32_t magic;
RTC_NOINIT_ATTR uint16_t current[kCount];
RTC_NOINIT_ATTR uint16_t previous[kCount];

// Short labels, in Stage order. The first entry has no label of its own: it is the
// baseline every later stage is measured from.
constexpr const char* kStageNames[kCount] = {"", "sd", "cfg", "in", "disp", "ban", "act"};

// millis() is 32-bit; a wake that reaches 65 seconds is already broken, and clamping
// keeps the display honest rather than wrapping to a small, believable-looking number.
uint16_t clampMs(const unsigned long ms) { return ms >= kUnset ? (kUnset - 1) : static_cast<uint16_t>(ms); }

}  // namespace

void beginWake() {
  if (magic == kMagic) {
    memcpy(previous, current, sizeof(previous));
  } else {
    for (uint8_t i = 0; i < kCount; i++) previous[i] = kUnset;
    magic = kMagic;
  }
  for (uint8_t i = 0; i < kCount; i++) current[i] = kUnset;
}

void mark(const Stage stage) {
  const uint8_t i = static_cast<uint8_t>(stage);
  if (i >= kCount) return;
  current[i] = clampMs(millis());
}

void formatPrevious(char* const out, const size_t outLen) {
  if (outLen == 0) return;
  out[0] = '\0';
  if (magic != kMagic || previous[0] == kUnset) return;

  size_t used = 0;
  uint16_t last = previous[0];
  for (uint8_t i = 1; i < kCount; i++) {
    if (previous[i] == kUnset) continue;
    // Stamps are monotonic in practice; guard anyway so a missed stage cannot underflow.
    const uint16_t delta = previous[i] > last ? static_cast<uint16_t>(previous[i] - last) : 0;
    const int n = snprintf(out + used, outLen - used, "%s%s %u", used == 0 ? "" : " ", kStageNames[i], delta);
    if (n <= 0 || static_cast<size_t>(n) >= outLen - used) return;
    used += static_cast<size_t>(n);
    last = previous[i];
  }
  if (used == 0) return;
  snprintf(out + used, outLen - used, " = %u", static_cast<unsigned>(last));
}

}  // namespace WakeTiming
