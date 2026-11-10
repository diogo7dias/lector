#include <gtest/gtest.h>

#include "sleep/WallpaperRefreshPolicy.h"

using wallpaper_refresh::isRefreshWake;
using wallpaper_refresh::shouldArm;
using wallpaper_refresh::SleepFacts;

namespace {
constexpr SleepFacts kArmable{true, true, 80};
}

TEST(WallpaperRefreshPolicy, ArmsOnlyForAWallpaperWithTheSwitchOn) {
  EXPECT_TRUE(shouldArm(kArmable));
  SleepFacts f = kArmable;
  f.settingOn = false;
  EXPECT_FALSE(shouldArm(f));
  f = kArmable;
  f.wallpaperOnPanel = false;
  EXPECT_FALSE(shouldArm(f));
}

TEST(WallpaperRefreshPolicy, ALowBatterySkipsIt) {
  SleepFacts f = kArmable;
  f.batteryPercent = 14;
  EXPECT_FALSE(shouldArm(f));
  f.batteryPercent = 15;
  EXPECT_TRUE(shouldArm(f));
}

TEST(WallpaperRefreshPolicy, AHeldPowerButtonIsAlwaysAnOrdinaryWake) {
  EXPECT_TRUE(isRefreshWake(true, true, false));
  EXPECT_FALSE(isRefreshWake(true, true, true));
  EXPECT_FALSE(isRefreshWake(false, true, false));
  EXPECT_FALSE(isRefreshWake(true, false, false));
}
