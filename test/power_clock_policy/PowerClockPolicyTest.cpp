#include <gtest/gtest.h>

#include "hal/PowerClockPolicy.h"

namespace {

using power_clock::isValidLowPowerFreqMhz;
using power_clock::shouldDisableBbpll;

TEST(PowerClockPolicy, DisablesBbpllBelow80MhzWhenIdleAndUnplugged) {
  EXPECT_TRUE(shouldDisableBbpll(40, false, false, false));
  EXPECT_TRUE(shouldDisableBbpll(20, false, false, false));
  EXPECT_TRUE(shouldDisableBbpll(10, false, false, false));
}

TEST(PowerClockPolicy, KeepsBbpllAtOrAbove80Mhz) {
  EXPECT_FALSE(shouldDisableBbpll(80, false, false, false));
  EXPECT_FALSE(shouldDisableBbpll(160, false, false, false));
  EXPECT_FALSE(shouldDisableBbpll(240, false, false, false));
}

TEST(PowerClockPolicy, KeepsBbpllWhenWifiActive) {
  EXPECT_FALSE(shouldDisableBbpll(40, true, false, false));
  EXPECT_FALSE(shouldDisableBbpll(10, true, false, false));
}

TEST(PowerClockPolicy, KeepsBbpllWhenUsbOrSerialJtagActive) {
  EXPECT_FALSE(shouldDisableBbpll(40, false, true, false));
  EXPECT_FALSE(shouldDisableBbpll(40, false, false, true));
  EXPECT_FALSE(shouldDisableBbpll(10, false, false, true));
  EXPECT_FALSE(shouldDisableBbpll(40, false, true, true));
}

TEST(PowerClockPolicy, ValidatesLowPowerFreqKnob) {
  EXPECT_TRUE(isValidLowPowerFreqMhz(10));
  EXPECT_TRUE(isValidLowPowerFreqMhz(20));
  EXPECT_TRUE(isValidLowPowerFreqMhz(40));
  EXPECT_TRUE(isValidLowPowerFreqMhz(80));

  EXPECT_FALSE(isValidLowPowerFreqMhz(0));
  EXPECT_FALSE(isValidLowPowerFreqMhz(15));
  EXPECT_FALSE(isValidLowPowerFreqMhz(60));
  EXPECT_FALSE(isValidLowPowerFreqMhz(160));
  EXPECT_FALSE(isValidLowPowerFreqMhz(240));
}

}  // namespace
