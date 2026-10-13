#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <time.h>

// Named time zones for the clock, as POSIX TZ rules so the C library applies summer
// time itself. Index 0 is "Manual": the fixed UTC offset setting is used instead.
// Zones without summer time need no entry; the manual offset already covers them.
namespace clock_zones {

constexpr const char* TZ[] = {
    nullptr,                               // Manual (UTC offset)
    "WET0WEST,M3.5.0/1,M10.5.0",           // Lisbon, London
    "CET-1CEST,M3.5.0,M10.5.0/3",          // Paris, Berlin, Madrid
    "EET-2EEST,M3.5.0/3,M10.5.0/4",        // Athens, Kyiv, Helsinki
    "EST5EDT,M3.2.0,M11.1.0",              // New York
    "CST6CDT,M3.2.0,M11.1.0",              // Chicago
    "MST7MDT,M3.2.0,M11.1.0",              // Denver
    "PST8PDT,M3.2.0,M11.1.0",              // Los Angeles
    "AEST-10AEDT,M10.1.0,M4.1.0/3",        // Sydney
    "NZST-12NZDT,M9.5.0,M4.1.0/3",         // Auckland
};
constexpr uint8_t COUNT = sizeof(TZ) / sizeof(TZ[0]);

// Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant's days_from_civil).
inline int64_t daysFromCivil(int y, const unsigned m, const unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

inline int64_t utcSeconds(const int year, const int month, const int day, const int hour, const int minute) {
  return daysFromCivil(year, month, day) * 86400 + hour * 3600 + minute * 60;
}

// Offset of `zone` from UTC at the instant `utc`, in minutes, summer time included.
// Sets the process TZ: nothing else in the firmware reads local time.
inline int offsetMinutes(const uint8_t zone, const int64_t utc) {
  if (zone == 0 || zone >= COUNT) return 0;
  static uint8_t applied = 0;
  if (applied != zone) {
    setenv("TZ", TZ[zone], 1);
    tzset();
    applied = zone;
  }
  const time_t t = static_cast<time_t>(utc);
  struct tm local;
  localtime_r(&t, &local);
  const int64_t asUtc =
      utcSeconds(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min) + local.tm_sec;
  return static_cast<int>((asUtc - utc) / 60);
}

}  // namespace clock_zones
