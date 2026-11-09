#pragma once

#include <cstdint>

// Battery percent read at most once per poll, however many times a screen measures or
// draws it. A gauge reports the percent itself; the ADC reading is smoothed in tenths.
class BatteryPercentPoll {
 public:
  static constexpr unsigned long POLL_MS = 1500;

  // read(percent) returns false when the reading failed.
  template <typename Read>
  uint16_t get(const bool hasGauge, const unsigned long now, Read read) {
    if (started && now - lastPollMs < POLL_MS) return hasGauge ? cached : cached / 10;

    lastPollMs = now;
    uint16_t percent = 0;
    const bool ok = read(percent);
    if (hasGauge) {
      started = true;
      if (ok) cached = percent;
      return cached;
    }
    cached = started ? (cached * 9 + percent * 10) / 10 : 10 * percent;
    started = true;
    return cached / 10;
  }

 private:
  int cached = 0;
  unsigned long lastPollMs = 0;
  bool started = false;
};
