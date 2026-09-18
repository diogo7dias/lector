#include <PowerReport.h>
#include <gtest/gtest.h>

#include <string>

// The sleep ratio is the only number that says whether enabling the IDF power
// manager changed anything, and it is read off a device this test suite cannot
// reach. So the arithmetic behind it is pinned here instead: a ratio that
// silently reads 0, or one that reads 3200%, would both be taken at face value.

TEST(PowerReport, RatioIsTenthsOfAPercent) {
  EXPECT_EQ(power_report::ratioPerMille(500'000, 1'000'000), 500);  // 50.0%
  EXPECT_EQ(power_report::ratioPerMille(412'000, 1'000'000), 412);  // 41.2%
  EXPECT_EQ(power_report::ratioPerMille(0, 1'000'000), 0);
}

TEST(PowerReport, RatioSaturatesAtFull) {
  // The accumulated figure comes from the sleep callback and uptime from
  // esp_timer; on a chip that has done nothing but sleep, rounding can put the
  // first a hair past the second. Print 100.0%, never more.
  EXPECT_EQ(power_report::ratioPerMille(1'000'050, 1'000'000), 1000);
}

TEST(PowerReport, RatioWithNoUptimeIsZeroNotADivideByZero) {
  EXPECT_EQ(power_report::ratioPerMille(0, 0), 0);
  EXPECT_EQ(power_report::ratioPerMille(5'000, 0), 0);
}

TEST(PowerReport, RatioSurvivesAnHoursLongSession) {
  // Six hours of uptime, four of them asleep. In microseconds that is past the
  // 32-bit range, which is why both inputs are 64-bit: truncating either one
  // here would report a plausible-looking but wrong ratio.
  constexpr uint64_t sixHoursUs = 6ULL * 3600ULL * 1'000'000ULL;
  constexpr uint64_t fourHoursUs = 4ULL * 3600ULL * 1'000'000ULL;
  EXPECT_EQ(power_report::ratioPerMille(fourHoursUs, sixHoursUs), 666);
}

TEST(PowerReport, MeanSleepLengthIsZeroWhenNothingSlept) { EXPECT_EQ(power_report::meanSleepUs(0, 0), 0u); }

TEST(PowerReport, MeanSleepLengthSeparatesNoWindowsFromShortWindows) {
  // 812 sleeps totalling 12.345 s: windows of about 15 ms, the healthy case.
  EXPECT_EQ(power_report::meanSleepUs(12'345'000, 812), 15'203u);
  // The same ratio reached in tiny slices instead says the idle poll period is
  // too short to pay for sleep entry, which is a different fix.
  EXPECT_EQ(power_report::meanSleepUs(12'345'000, 12'345), 1'000u);
}

TEST(PowerReport, FormatCarriesEveryNumberTheReadoutPromises) {
  char line[160] = {};
  power_report::format(line, sizeof(line), 240, 12'345'000, 30'000'000, 812, "render,usb");
  EXPECT_STREQ(line, "pm cpu 240 MHz sleep 41.1% 12345 of 30000 ms over 812 sleeps mean 15 ms locks render,usb");
}

TEST(PowerReport, FormatSaysNoneRatherThanAnEmptyLockList) {
  char line[160] = {};
  power_report::format(line, sizeof(line), 10, 0, 1'000'000, 0, "");
  EXPECT_NE(std::string(line).find("locks none"), std::string::npos);
  power_report::format(line, sizeof(line), 10, 0, 1'000'000, 0, nullptr);
  EXPECT_NE(std::string(line).find("locks none"), std::string::npos);
}

TEST(PowerReport, FormatTruncatesRatherThanOverrunning) {
  char line[16] = {};
  const int wanted = power_report::format(line, sizeof(line), 240, 1, 2, 3, "render");
  EXPECT_GT(wanted, static_cast<int>(sizeof(line)));  // snprintf reports what it wanted
  EXPECT_EQ(line[sizeof(line) - 1], '\0');
}
