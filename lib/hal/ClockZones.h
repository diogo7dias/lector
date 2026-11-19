#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Time zones for the clock, CrossPoint 2.0.0's table in CrossPoint's order. Each entry carries
// the POSIX TZ rule the C library applies in localtime_r(), so summer time follows the date
// from a clock that keeps UTC.
//
// APPEND ONLY: settings.json stores clockTimezone as an index into TABLE, the same index
// CrossPoint stores, so reordering or removing an entry moves everyone's saved zone.
namespace clock_zones {

struct Zone {
  const char* name;     // shown in the picker; city and region names stay untranslated
  const char* posixTz;  // POSIX TZ rule, e.g. "CET-1CEST,M3.5.0,M10.5.0/3"
  int8_t stdOffsetQ;    // standard (non-summer) offset from UTC in quarter hours
};

// Ordered west to east by standard offset; the fixed UTC+/-HH:MM block closes the table.
// The POSIX offset sign is inverted relative to the label.
inline constexpr Zone TABLE[] = {
    {"Midway", "SST11", -44},
    {"Honolulu", "HST10", -40},
    {"Anchorage", "AKST9AKDT,M3.2.0,M11.1.0", -36},
    {"Los Angeles / Vancouver", "PST8PDT,M3.2.0,M11.1.0", -32},
    {"Denver / Edmonton", "MST7MDT,M3.2.0,M11.1.0", -28},
    {"Phoenix", "MST7", -28},
    {"Chicago / Winnipeg", "CST6CDT,M3.2.0,M11.1.0", -24},
    {"Mexico City", "CST6", -24},
    {"New York / Toronto", "EST5EDT,M3.2.0,M11.1.0", -20},
    {"Bogota / Lima", "COT5", -20},
    {"Halifax", "AST4ADT,M3.2.0,M11.1.0", -16},
    {"Caracas / La Paz", "VET4", -16},
    {"Santiago", "CLT4CLST,M9.1.6/24,M4.1.6/24", -16},
    {"St. John's", "NST3:30NDT,M3.2.0,M11.1.0", -14},
    {"Buenos Aires / Montevideo", "ART3", -12},
    {"Sao Paulo", "BRT3", -12},
    {"Azores", "AZOT1AZOST,M3.5.0/0,M10.5.0/1", -4},
    {"UTC", "UTC0", 0},
    {"London / Dublin / Lisbon", "GMT0BST,M3.5.0/1,M10.5.0", 0},
    {"Berlin / Paris / Madrid / Rome", "CET-1CEST,M3.5.0,M10.5.0/3", 4},
    {"Lagos / Algiers", "WAT-1", 4},
    {"Athens / Helsinki / Kyiv", "EET-2EEST,M3.5.0/3,M10.5.0/4", 8},
    {"Cairo", "EET-2EEST,M4.5.5/0,M10.5.4/24", 8},
    {"Jerusalem", "IST-2IDT,M3.4.4/26,M10.5.0", 8},
    {"Johannesburg", "SAST-2", 8},
    {"Moscow / Istanbul / Riyadh", "MSK-3", 12},
    {"Nairobi", "EAT-3", 12},
    {"Tehran", "IRST-3:30", 14},
    {"Dubai / Tbilisi", "GST-4", 16},
    {"Kabul", "AFT-4:30", 18},
    {"Karachi / Tashkent", "PKT-5", 20},
    {"India / Colombo", "IST-5:30", 22},
    {"Kathmandu", "NPT-5:45", 23},
    {"Dhaka / Almaty", "BST-6", 24},
    {"Yangon", "MMT-6:30", 26},
    {"Bangkok / Jakarta / Hanoi", "ICT-7", 28},
    {"China / Hong Kong / Taipei", "CST-8", 32},
    {"Singapore / Manila / Kuala Lumpur", "SGT-8", 32},
    {"Perth", "AWST-8", 32},
    {"Tokyo / Seoul", "JST-9", 36},
    {"Darwin", "ACST-9:30", 38},
    {"Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3", 38},
    {"Brisbane / Guam", "AEST-10", 40},
    {"Sydney / Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3", 40},
    {"Honiara / Noumea", "SBT-11", 44},
    {"Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3", 48},
    {"Fiji", "FJT-12", 48},
    {"Nuku'alofa", "TOT-13", 52},
    {"Kiritimati", "LINT-14", 56},
    // Fixed offsets without summer time: the manual choice for a place the list misses.
    {"UTC-12:00", "UTC12", -48},
    {"UTC-11:30", "UTC11:30", -46},
    {"UTC-11:00", "UTC11", -44},
    {"UTC-10:30", "UTC10:30", -42},
    {"UTC-10:00", "UTC10", -40},
    {"UTC-09:30", "UTC9:30", -38},
    {"UTC-09:00", "UTC9", -36},
    {"UTC-08:30", "UTC8:30", -34},
    {"UTC-08:00", "UTC8", -32},
    {"UTC-07:30", "UTC7:30", -30},
    {"UTC-07:00", "UTC7", -28},
    {"UTC-06:30", "UTC6:30", -26},
    {"UTC-06:00", "UTC6", -24},
    {"UTC-05:30", "UTC5:30", -22},
    {"UTC-05:00", "UTC5", -20},
    {"UTC-04:30", "UTC4:30", -18},
    {"UTC-04:00", "UTC4", -16},
    {"UTC-03:30", "UTC3:30", -14},
    {"UTC-03:00", "UTC3", -12},
    {"UTC-02:30", "UTC2:30", -10},
    {"UTC-02:00", "UTC2", -8},
    {"UTC-01:30", "UTC1:30", -6},
    {"UTC-01:00", "UTC1", -4},
    {"UTC-00:30", "UTC0:30", -2},
    {"UTC+00:30", "UTC-0:30", 2},
    {"UTC+01:00", "UTC-1", 4},
    {"UTC+01:30", "UTC-1:30", 6},
    {"UTC+02:00", "UTC-2", 8},
    {"UTC+02:30", "UTC-2:30", 10},
    {"UTC+03:00", "UTC-3", 12},
    {"UTC+03:30", "UTC-3:30", 14},
    {"UTC+04:00", "UTC-4", 16},
    {"UTC+04:30", "UTC-4:30", 18},
    {"UTC+05:00", "UTC-5", 20},
    {"UTC+05:30", "UTC-5:30", 22},
    {"UTC+05:45", "UTC-5:45", 23},
    {"UTC+06:00", "UTC-6", 24},
    {"UTC+06:30", "UTC-6:30", 26},
    {"UTC+07:00", "UTC-7", 28},
    {"UTC+07:30", "UTC-7:30", 30},
    {"UTC+08:00", "UTC-8", 32},
    {"UTC+08:30", "UTC-8:30", 34},
    {"UTC+08:45", "UTC-8:45", 35},
    {"UTC+09:00", "UTC-9", 36},
    {"UTC+09:30", "UTC-9:30", 38},
    {"UTC+10:00", "UTC-10", 40},
    {"UTC+10:30", "UTC-10:30", 42},
    {"UTC+11:00", "UTC-11", 44},
    {"UTC+11:30", "UTC-11:30", 46},
    {"UTC+12:00", "UTC-12", 48},
    {"UTC+12:30", "UTC-12:30", 50},
    {"UTC+12:45", "UTC-12:45", 51},
    {"UTC+13:00", "UTC-13", 52},
    {"UTC+13:30", "UTC-13:30", 54},
    {"UTC+14:00", "UTC-14", 56},
};
constexpr uint8_t COUNT = sizeof(TABLE) / sizeof(TABLE[0]);
constexpr uint8_t UTC_INDEX = 17;
constexpr uint8_t FIXED_FIRST = 49;  // first "UTC-12:00" entry
static_assert(sizeof(TABLE) / sizeof(TABLE[0]) < 255, "255 is the 'never chosen' value of clockTimezone");
static_assert(TABLE[UTC_INDEX].stdOffsetQ == 0, "UTC_INDEX must point at the UTC entry");
static_assert(TABLE[FIXED_FIRST].stdOffsetQ == -48, "FIXED_FIRST must point at UTC-12:00");

// clockDst: follow the zone's own summer-time rule, or force summer time on or off. Forcing
// is the escape for a country that changed its law before the firmware did.
constexpr uint8_t DST_AUTO = 0, DST_ON = 1, DST_OFF = 2, DST_COUNT = 3;

// The table index a saved clockTimezone means: 255, or anything past the table, is "never
// chosen" and reads as UTC.
inline uint8_t activeIndex(const uint8_t zone) { return zone < COUNT ? zone : UTC_INDEX; }

// "UTC", "UTC+5:30", "UTC-8:00": the zone's standard offset.
inline void formatOffset(const uint8_t zone, char* buf, const size_t size) {
  const int q = TABLE[activeIndex(zone)].stdOffsetQ;
  if (q == 0) {
    snprintf(buf, size, "UTC");
    return;
  }
  const int a = q < 0 ? -q : q;
  snprintf(buf, size, "UTC%c%d:%02d", q < 0 ? '-' : '+', a / 4, (a % 4) * 15);
}

// "Name (UTC+5:30)", for the web page's drop-down.
inline void label(const uint8_t zone, char* buf, const size_t size) {
  char offset[12];
  formatOffset(zone, offset, sizeof(offset));
  snprintf(buf, size, "%s (%s)", TABLE[activeIndex(zone)].name, offset);
}

// The POSIX TZ rule for a zone and its summer-time override. Forced on or off swaps the rule
// for a fixed offset (standard, plus one hour when on) written into `buf`.
inline const char* rule(const uint8_t zone, const uint8_t dst, char* buf, const size_t size) {
  const Zone& z = TABLE[activeIndex(zone)];
  if (dst != DST_ON && dst != DST_OFF) return z.posixTz;
  const int q = z.stdOffsetQ + (dst == DST_ON ? 4 : 0);
  const int a = q < 0 ? -q : q;
  snprintf(buf, size, "UTC%c%d:%02d", q < 0 ? '+' : '-', a / 4, (a % 4) * 15);
  return buf;
}

// The zone a settings file means by the two keys 0.58.0 wrote (and CrossPoint 1.x wrote one
// of): a city from the old ten-name list, and a fixed UTC offset in quarter hours biased by
// 48. A city keeps its summer time. Without one the offset was fixed all year, so it maps to
// the fixed entry with that offset, never to a region that happens to share its winter
// offset: someone who typed in their summer offset would land an hour out half the year.
// An offset no entry has (a quarter past an hour, which no country uses) takes the nearest
// entry, UTC itself included.
inline uint8_t fromLegacy(const uint8_t city, const uint8_t offsetQBiased) {
  // 0 = Manual, then Lisbon, Paris, Athens, New York, Chicago, Denver, Los Angeles, Sydney,
  // Auckland: the rows of this table that run the same rule.
  constexpr uint8_t CITY[] = {UTC_INDEX, 18, 19, 21, 8, 6, 4, 3, 43, 45};
  if (city > 0 && city < sizeof(CITY)) return CITY[city];
  const int q = (offsetQBiased > 104 ? 104 : static_cast<int>(offsetQBiased)) - 48;
  uint8_t best = UTC_INDEX;
  int bestDistance = q < 0 ? -q : q;
  for (uint8_t i = FIXED_FIRST; i < COUNT; i++) {
    const int d = TABLE[i].stdOffsetQ > q ? TABLE[i].stdOffsetQ - q : q - TABLE[i].stdOffsetQ;
    if (d < bestDistance) {
      best = i;
      bestDistance = d;
    }
  }
  return best;
}

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

// Offset of the zone from UTC at the instant `utc`, in minutes, summer time included. Every
// local time in the firmware comes from here (status bar and Home clock, Light Schedule,
// reading journal), so they cannot disagree. Sets the process TZ each time rather than
// remembering that it did: an NTP sync resets it to UTC, and the next answer would then be
// the wrong one.
inline int offsetMinutes(const uint8_t zone, const uint8_t dst, const int64_t utc) {
  char fixed[16];
  setenv("TZ", rule(zone, dst, fixed, sizeof(fixed)), 1);
  tzset();
  const time_t t = static_cast<time_t>(utc);
  struct tm local;
  localtime_r(&t, &local);
  const int64_t asUtc =
      utcSeconds(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min) + local.tm_sec;
  return static_cast<int>((asUtc - utc) / 60);
}

}  // namespace clock_zones
