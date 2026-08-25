#include "SleepTiming.h"

#include <Arduino.h>

#include <cstdio>

namespace SleepTiming {
namespace {

// The wallpaper face is the longest path: favourites, popup, four index-pick stages, the
// file open, decode, the three grayscale passes, face, state -- thirteen today, with room
// left before anyone has to think about it.
constexpr size_t kMax = 24;

const char* names[kMax];
uint16_t stamps[kMax];
size_t used = 0;
unsigned long startMs = 0;

}  // namespace

void begin() {
  used = 0;
  startMs = millis();
}

void mark(const char* name) {
  if (used >= kMax || startMs == 0) return;
  const unsigned long elapsed = millis() - startMs;
  names[used] = name;
  // A lock that reaches 65 seconds is broken anyway; clamping keeps the report honest
  // instead of wrapping to a small, believable-looking number.
  stamps[used] = elapsed > 0xFFFE ? 0xFFFE : static_cast<uint16_t>(elapsed);
  used++;
}

void format(char* out, size_t outLen) {
  if (outLen == 0) return;
  out[0] = '\0';
  size_t at = 0;
  for (size_t i = 0; i < used && at + 1 < outLen; i++) {
    const int written =
        snprintf(out + at, outLen - at, i == 0 ? "%s=%u" : " %s=%u", names[i], static_cast<unsigned>(stamps[i]));
    if (written <= 0) break;
    at += static_cast<size_t>(written);
    if (at >= outLen) break;
  }
}

}  // namespace SleepTiming
