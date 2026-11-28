#include <gtest/gtest.h>

#include "hal/IdleSleepPolicy.h"

namespace {

using idle_sleep::canLightSleep;
using idle_sleep::Conditions;
using idle_sleep::SLICE_TOUCH_ASLEEP_MS;
using idle_sleep::SLICE_TOUCH_AWAKE_MS;
using idle_sleep::sliceDurationMs;
using idle_sleep::touchWakeHighLevel;

Conditions readyConditions() {
  Conditions c{};
  c.renderIdle = true;
  c.gpioWakeArmed = true;
  return c;
}

TEST(IdleSleepPolicy, AllowsSleepOnlyWhenEveryGuardHolds) {
  EXPECT_TRUE(canLightSleep(readyConditions()));

  {
    auto c = readyConditions();
    c.wifiActive = true;
    EXPECT_FALSE(canLightSleep(c));
  }
  {
    auto c = readyConditions();
    c.usbConnected = true;
    EXPECT_FALSE(canLightSleep(c));
  }
  {
    auto c = readyConditions();
    c.usbSerialJtagActive = true;
    EXPECT_FALSE(canLightSleep(c));
  }
  {
    auto c = readyConditions();
    c.renderIdle = false;
    EXPECT_FALSE(canLightSleep(c));
  }
  {
    auto c = readyConditions();
    c.powerLockHeld = true;
    EXPECT_FALSE(canLightSleep(c));
  }
  {
    auto c = readyConditions();
    c.rawInputActive = true;
    EXPECT_FALSE(canLightSleep(c));
  }
  {
    auto c = readyConditions();
    c.touchHeld = true;
    EXPECT_FALSE(canLightSleep(c));
  }
  {
    auto c = readyConditions();
    c.gpioWakeArmed = false;
    EXPECT_FALSE(canLightSleep(c));
  }
}

TEST(IdleSleepPolicy, SlicesRespectTouchStateAndDeadlines) {
  EXPECT_EQ(sliceDurationMs(/*touchAsleep=*/false, 10000), SLICE_TOUCH_AWAKE_MS);
  EXPECT_EQ(sliceDurationMs(/*touchAsleep=*/true, 10000), SLICE_TOUCH_ASLEEP_MS);
  EXPECT_EQ(sliceDurationMs(/*touchAsleep=*/false, 15), 15U);
  EXPECT_EQ(sliceDurationMs(/*touchAsleep=*/true, 80), 80U);
}

TEST(IdleSleepPolicy, TouchWakePolarityOpposesIdleLevel) {
  EXPECT_TRUE(touchWakeHighLevel(0));
  EXPECT_FALSE(touchWakeHighLevel(1));
}

}  // namespace
