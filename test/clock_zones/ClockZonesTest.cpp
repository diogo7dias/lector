#include <gtest/gtest.h>

#include "ClockZones.h"

using clock_zones::offsetMinutes;
using clock_zones::utcSeconds;

constexpr uint8_t LISBON = 1, PARIS = 2, NEW_YORK = 4, SYDNEY = 8;

TEST(ClockZones, UtcSecondsMatchesTheEpoch) {
  EXPECT_EQ(0, utcSeconds(1970, 1, 1, 0, 0));
  EXPECT_EQ(1798761600, utcSeconds(2027, 1, 1, 0, 0));
}

// EU summer time ends 2026-10-25 at 01:00 UTC.
TEST(ClockZones, LisbonLeavesSummerTimeOnTheLastSundayOfOctober) {
  EXPECT_EQ(60, offsetMinutes(LISBON, utcSeconds(2026, 10, 25, 0, 59)));
  EXPECT_EQ(0, offsetMinutes(LISBON, utcSeconds(2026, 10, 25, 1, 0)));
  EXPECT_EQ(60, offsetMinutes(PARIS, utcSeconds(2026, 10, 25, 1, 0)));
  EXPECT_EQ(120, offsetMinutes(PARIS, utcSeconds(2026, 10, 25, 0, 59)));
}

// And starts again 2027-03-28 at 01:00 UTC.
TEST(ClockZones, LisbonEntersSummerTimeOnTheLastSundayOfMarch) {
  EXPECT_EQ(0, offsetMinutes(LISBON, utcSeconds(2027, 3, 28, 0, 59)));
  EXPECT_EQ(60, offsetMinutes(LISBON, utcSeconds(2027, 3, 28, 1, 0)));
}

TEST(ClockZones, SouthernAndAmericanRules) {
  EXPECT_EQ(-240, offsetMinutes(NEW_YORK, utcSeconds(2026, 7, 1, 12, 0)));
  EXPECT_EQ(-300, offsetMinutes(NEW_YORK, utcSeconds(2026, 12, 1, 12, 0)));
  EXPECT_EQ(660, offsetMinutes(SYDNEY, utcSeconds(2026, 12, 1, 12, 0)));
  EXPECT_EQ(600, offsetMinutes(SYDNEY, utcSeconds(2026, 7, 1, 12, 0)));
}

TEST(ClockZones, ManualAndOutOfRangeAddNothing) {
  EXPECT_EQ(0, offsetMinutes(0, utcSeconds(2026, 7, 1, 12, 0)));
  EXPECT_EQ(0, offsetMinutes(clock_zones::COUNT, utcSeconds(2026, 7, 1, 12, 0)));
}
