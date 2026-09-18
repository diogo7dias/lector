#pragma once

#include <cstdint>
#include <cstdio>

// The one line that says whether the power manager is actually doing anything.
//
// It exists because the change it measures cannot be judged from a build: the
// IDF power manager either drops the clock and light-sleeps in the gaps between
// input polls, or it silently never does, and the only difference on the outside
// is battery life measured over days. This prints the sleep ratio instead, which
// is the same fact available in one reading session.
//
// Pure arithmetic and one snprintf, so the host tests can cover it
// (test/power_report). Nothing here touches the ESP-IDF.
namespace power_report {

// Share of wall-clock time spent in power-manager light sleep, in tenths of a
// percent. Integer maths on purpose: the RISC-V and Xtensa targets here have no
// hardware double, and a "%f" would drag the soft-float formatter into a build
// that otherwise never needs it.
//
// Saturates at 1000 (100.0%) rather than printing an impossible ratio: the two
// inputs come from different clocks (the accumulated figure from the sleep
// callback, uptime from esp_timer), so rounding at the edges can put slept a few
// microseconds past uptime on a chip that has done nothing but sleep.
constexpr uint16_t ratioPerMille(const uint64_t sleptUs, const uint64_t uptimeUs) {
  if (uptimeUs == 0) return 0;
  const uint64_t perMille = sleptUs * 1000ULL / uptimeUs;
  return perMille > 1000ULL ? 1000 : static_cast<uint16_t>(perMille);
}

// Mean length of one sleep, in microseconds; 0 when nothing slept. A low ratio
// with a healthy mean means the chip rarely got an idle window. A low ratio with
// a mean of a millisecond or two means it got windows and they were too short to
// pay for the entry and exit, which is a different fix (raise the idle poll
// period, not the thresholds).
constexpr uint32_t meanSleepUs(const uint64_t sleptUs, const uint32_t sleepCount) {
  if (sleepCount == 0) return 0;
  return static_cast<uint32_t>(sleptUs / sleepCount);
}

// Formats into `out`, e.g.
//   pm cpu 160 MHz sleep 41.1% 12345 of 30000 ms over 812 sleeps mean 15 ms locks render,usb
// `lockNames` is a comma-separated list of the locks held right now, or "none".
// Returns the snprintf return value (the length it wanted to write).
inline int format(char* const out, const size_t outLen, const unsigned cpuMhz, const uint64_t sleptUs,
                  const uint64_t uptimeUs, const uint32_t sleepCount, const char* const lockNames) {
  const uint16_t perMille = ratioPerMille(sleptUs, uptimeUs);
  return snprintf(out, outLen, "pm cpu %u MHz sleep %u.%u%% %lu of %lu ms over %lu sleeps mean %lu ms locks %s", cpuMhz,
                  static_cast<unsigned>(perMille / 10), static_cast<unsigned>(perMille % 10),
                  static_cast<unsigned long>(sleptUs / 1000ULL), static_cast<unsigned long>(uptimeUs / 1000ULL),
                  static_cast<unsigned long>(sleepCount),
                  static_cast<unsigned long>(meanSleepUs(sleptUs, sleepCount) / 1000UL),
                  lockNames && lockNames[0] ? lockNames : "none");
}

}  // namespace power_report
