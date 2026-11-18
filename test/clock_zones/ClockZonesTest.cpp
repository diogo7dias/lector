#include <gtest/gtest.h>

#include <cstring>

#include "ClockZones.h"

using namespace clock_zones;

namespace {
constexpr uint8_t LOS_ANGELES = 3, NEW_YORK = 8, SANTIAGO = 12, LONDON = 18, BERLIN = 19, ATHENS = 21, CAIRO = 22,
                  JERUSALEM = 23, KATHMANDU = 32, TOKYO = 39, SYDNEY = 43, AUCKLAND = 45, PHOENIX = 5;

int off(const uint8_t zone, const int y, const int mo, const int d, const int h, const int mi,
        const uint8_t dst = DST_AUTO) {
  return offsetMinutes(zone, dst, utcSeconds(y, mo, d, h, mi));
}
}  // namespace

TEST(ClockZones, UtcSecondsMatchesTheEpoch) {
  EXPECT_EQ(0, utcSeconds(1970, 1, 1, 0, 0));
  EXPECT_EQ(1798761600, utcSeconds(2027, 1, 1, 0, 0));
}

// The table is CrossPoint 2.0.0's, and the index is what a settings file stores.
TEST(ClockZones, TableKeepsCrossPointsOrder) {
  EXPECT_EQ(104, COUNT);
  EXPECT_STREQ("UTC", TABLE[UTC_INDEX].name);
  EXPECT_STREQ("London / Dublin / Lisbon", TABLE[LONDON].name);
  EXPECT_STREQ("Berlin / Paris / Madrid / Rome", TABLE[BERLIN].name);
  EXPECT_STREQ("Sydney / Melbourne", TABLE[SYDNEY].name);
  EXPECT_STREQ("Auckland", TABLE[AUCKLAND].name);
  EXPECT_STREQ("UTC-12:00", TABLE[FIXED_FIRST].name);
  EXPECT_STREQ("UTC+14:00", TABLE[COUNT - 1].name);
}

// Every entry's stored standard offset is the smaller of its January and July offsets, so the
// number shown beside a name is the one its rule produces.
TEST(ClockZones, StandardOffsetMatchesTheRule) {
  for (uint8_t i = 0; i < COUNT; i++) {
    const int jan = off(i, 2026, 1, 15, 12, 0);
    const int jul = off(i, 2026, 7, 15, 12, 0);
    EXPECT_EQ(TABLE[i].stdOffsetQ * 15, jan < jul ? jan : jul) << TABLE[i].name;
  }
}

// EU summer time ends 2026-10-25 at 01:00 UTC.
TEST(ClockZones, LondonLeavesSummerTimeOnTheLastSundayOfOctober) {
  EXPECT_EQ(60, off(LONDON, 2026, 10, 25, 0, 59));
  EXPECT_EQ(0, off(LONDON, 2026, 10, 25, 1, 0));
  EXPECT_EQ(120, off(BERLIN, 2026, 10, 25, 0, 59));
  EXPECT_EQ(60, off(BERLIN, 2026, 10, 25, 1, 0));
}

// And starts again 2027-03-28 at 01:00 UTC.
TEST(ClockZones, LondonEntersSummerTimeOnTheLastSundayOfMarch) {
  EXPECT_EQ(0, off(LONDON, 2027, 3, 28, 0, 59));
  EXPECT_EQ(60, off(LONDON, 2027, 3, 28, 1, 0));
  EXPECT_EQ(60, off(BERLIN, 2027, 3, 28, 0, 59));
  EXPECT_EQ(120, off(BERLIN, 2027, 3, 28, 1, 0));
}

TEST(ClockZones, AmericanAndSouthernRules) {
  EXPECT_EQ(-240, off(NEW_YORK, 2026, 7, 1, 12, 0));
  EXPECT_EQ(-300, off(NEW_YORK, 2026, 12, 1, 12, 0));
  EXPECT_EQ(-420, off(LOS_ANGELES, 2026, 7, 1, 12, 0));
  EXPECT_EQ(660, off(SYDNEY, 2026, 12, 1, 12, 0));
  EXPECT_EQ(600, off(SYDNEY, 2026, 7, 1, 12, 0));
  EXPECT_EQ(780, off(AUCKLAND, 2026, 12, 1, 12, 0));
  EXPECT_EQ(720, off(AUCKLAND, 2026, 7, 1, 12, 0));
  EXPECT_EQ(-180, off(SANTIAGO, 2026, 12, 1, 12, 0));
  EXPECT_EQ(-240, off(SANTIAGO, 2026, 7, 1, 12, 0));
}

// Rules whose switch hour is past midnight of the day before or after (Cairo "/24",
// Jerusalem "/26") are the ones a hand-rolled parser tends to get wrong.
TEST(ClockZones, LateHourRules) {
  EXPECT_EQ(120, off(CAIRO, 2026, 1, 15, 12, 0));
  EXPECT_EQ(180, off(CAIRO, 2026, 7, 15, 12, 0));
  EXPECT_EQ(120, off(JERUSALEM, 2026, 1, 15, 12, 0));
  EXPECT_EQ(180, off(JERUSALEM, 2026, 7, 15, 12, 0));
}

TEST(ClockZones, ZonesWithoutSummerTimeStayPut) {
  EXPECT_EQ(540, off(TOKYO, 2026, 1, 1, 0, 0));
  EXPECT_EQ(540, off(TOKYO, 2026, 7, 1, 0, 0));
  EXPECT_EQ(345, off(KATHMANDU, 2026, 7, 1, 0, 0));
  EXPECT_EQ(-420, off(PHOENIX, 2026, 7, 1, 0, 0));
  EXPECT_EQ(-420, off(PHOENIX, 2026, 1, 1, 0, 0));
}

TEST(ClockZones, ForcedSummerTimeReplacesTheRule) {
  EXPECT_EQ(120, off(BERLIN, 2026, 1, 15, 12, 0, DST_ON));
  EXPECT_EQ(60, off(BERLIN, 2026, 7, 15, 12, 0, DST_OFF));
  EXPECT_EQ(600, off(TOKYO, 2026, 1, 15, 12, 0, DST_ON));
  EXPECT_EQ(345 + 60, off(KATHMANDU, 2026, 1, 15, 12, 0, DST_ON));
  EXPECT_EQ(0, off(UTC_INDEX, 2026, 7, 15, 12, 0, DST_OFF));
  EXPECT_EQ(60, off(UTC_INDEX, 2026, 7, 15, 12, 0, DST_ON));
  // A value past the three choices follows the rule, like Auto.
  EXPECT_EQ(120, off(BERLIN, 2026, 7, 15, 12, 0, 9));
}

// "Never chosen" (255), or any index past the table, reads as UTC.
TEST(ClockZones, UnsetAndOutOfRangeReadAsUtc) {
  EXPECT_EQ(UTC_INDEX, activeIndex(255));
  EXPECT_EQ(UTC_INDEX, activeIndex(COUNT));
  EXPECT_EQ(BERLIN, activeIndex(BERLIN));
  EXPECT_EQ(0, off(255, 2026, 7, 1, 12, 0));
  EXPECT_EQ(0, off(COUNT, 2026, 7, 1, 12, 0));
}

// An NTP sync leaves the process TZ at UTC. The next answer must still follow the zone.
TEST(ClockZones, AnswersAgainAfterSomethingElseSetsTheProcessZone) {
  EXPECT_EQ(120, off(BERLIN, 2026, 7, 1, 12, 0));
  setenv("TZ", "UTC0", 1);
  tzset();
  EXPECT_EQ(120, off(BERLIN, 2026, 7, 1, 12, 0));
}

TEST(ClockZones, OffsetAndLabelText) {
  char buf[48];
  formatOffset(UTC_INDEX, buf, sizeof(buf));
  EXPECT_STREQ("UTC", buf);
  formatOffset(KATHMANDU, buf, sizeof(buf));
  EXPECT_STREQ("UTC+5:45", buf);
  formatOffset(LOS_ANGELES, buf, sizeof(buf));
  EXPECT_STREQ("UTC-8:00", buf);
  formatOffset(255, buf, sizeof(buf));
  EXPECT_STREQ("UTC", buf);
  label(BERLIN, buf, sizeof(buf));
  EXPECT_STREQ("Berlin / Paris / Madrid / Rome (UTC+1:00)", buf);
  // The longest name still fits the 64 bytes the web page gives it.
  size_t longest = 0;
  for (uint8_t i = 0; i < COUNT; i++) {
    label(i, buf, sizeof(buf));
    if (strlen(buf) > longest) longest = strlen(buf);
  }
  EXPECT_LT(longest, 63u);
}

// What 0.58.0 stored: a city from ten (0 = none) and a fixed offset biased by 48. The zone it
// migrates to must show the same local time at every hour of two years, summer time included.
namespace {
const char* const OLD_CITY_RULE[] = {
    nullptr,
    "WET0WEST,M3.5.0/1,M10.5.0",     // Lisbon, London
    "CET-1CEST,M3.5.0,M10.5.0/3",    // Paris, Berlin, Madrid
    "EET-2EEST,M3.5.0/3,M10.5.0/4",  // Athens, Kyiv, Helsinki
    "EST5EDT,M3.2.0,M11.1.0",        // New York
    "CST6CDT,M3.2.0,M11.1.0",        // Chicago
    "MST7MDT,M3.2.0,M11.1.0",        // Denver
    "PST8PDT,M3.2.0,M11.1.0",        // Los Angeles
    "AEST-10AEDT,M10.1.0,M4.1.0/3",  // Sydney
    "NZST-12NZDT,M9.5.0,M4.1.0/3",   // Auckland
};

int oldRuleOffset(const char* rule, const int64_t utc) {
  setenv("TZ", rule, 1);
  tzset();
  const time_t t = static_cast<time_t>(utc);
  struct tm local;
  localtime_r(&t, &local);
  return static_cast<int>(
      (utcSeconds(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min) - utc) / 60);
}
}  // namespace

TEST(ClockZonesLegacy, EachOldCityKeepsItsLocalTimeAllYear) {
  const int64_t from = utcSeconds(2026, 1, 1, 0, 0);
  for (uint8_t city = 1; city < 10; city++) {
    const uint8_t zone = fromLegacy(city, 48);
    for (int64_t utc = from; utc < from + 2 * 366 * 86400; utc += 3600) {
      ASSERT_EQ(oldRuleOffset(OLD_CITY_RULE[city], utc), offsetMinutes(zone, DST_AUTO, utc))
          << "city " << int(city) << " -> " << TABLE[zone].name << " at " << utc;
    }
  }
}

// The city wins over the offset, as it did: a file with both kept using the city.
TEST(ClockZonesLegacy, ACityOverridesTheOffset) {
  EXPECT_EQ(BERLIN, fromLegacy(2, 80));
  EXPECT_EQ(NEW_YORK, fromLegacy(4, 0));
}

// No city: the offset was fixed all year, so it keeps its value in January and in July.
TEST(ClockZonesLegacy, AFixedOffsetStaysFixedAllYear) {
  for (int q = 0; q <= 104; q++) {
    const uint8_t zone = fromLegacy(0, static_cast<uint8_t>(q));
    const int want = (q - 48) * 15;
    const int jan = off(zone, 2026, 1, 15, 12, 0);
    const int jul = off(zone, 2026, 7, 15, 12, 0);
    EXPECT_EQ(jan, jul) << "q " << q;
    if (q % 2 == 0 || q == 71 || q == 83 || q == 99) {
      EXPECT_EQ(want, jan) << "q " << q;  // every half hour, and +5:45, +8:45, +12:45
    } else {
      EXPECT_LE(abs(want - jan), 15) << "q " << q;  // a quarter past: nearest entry
    }
  }
}

// Someone who set their summer offset by hand (UTC+2 in Berlin) must not be moved onto Berlin's
// rule, which would show UTC+1 for half the year.
TEST(ClockZonesLegacy, ASummerOffsetDoesNotBecomeARegion) {
  const uint8_t zone = fromLegacy(0, 56);  // 48 + 8 quarter hours = UTC+2
  EXPECT_STREQ("UTC+02:00", TABLE[zone].name);
  EXPECT_EQ(120, off(zone, 2026, 1, 15, 12, 0));
  EXPECT_EQ(120, off(zone, 2026, 7, 15, 12, 0));
}

TEST(ClockZonesLegacy, DefaultsAndCorruptValuesReadAsUtc) {
  EXPECT_EQ(UTC_INDEX, fromLegacy(0, 48));
  EXPECT_STREQ("UTC+14:00", TABLE[fromLegacy(0, 104)].name);
  EXPECT_STREQ("UTC+14:00", TABLE[fromLegacy(0, 255)].name);  // clamped like the old code did
  EXPECT_STREQ("UTC-12:00", TABLE[fromLegacy(0, 0)].name);
  EXPECT_EQ(UTC_INDEX, fromLegacy(200, 48));  // a city past the old list is no city
}
