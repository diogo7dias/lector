#include <gtest/gtest.h>

#include "src/network/WebPowerSavePolicy.h"

namespace {

TEST(WebPowerSavePolicy, StaysFullPowerBeforeIdleTimeoutThenDropsOnce) {
  web_power_save::State state;
  state.reset(1000);

  EXPECT_FALSE(state.shouldEnterModemSleep(15999, /*apMode=*/false, /*busy=*/false));
  EXPECT_FALSE(state.modemSleep);

  EXPECT_TRUE(state.shouldEnterModemSleep(16000, /*apMode=*/false, /*busy=*/false));
  EXPECT_TRUE(state.modemSleep);

  // Already in modem sleep: must not re-fire WiFi.setSleep every loop tick.
  EXPECT_FALSE(state.shouldEnterModemSleep(20000, /*apMode=*/false, /*busy=*/false));
  EXPECT_TRUE(state.modemSleep);
}

TEST(WebPowerSavePolicy, TrafficRestoresFullPowerAndRestartsIdleTimer) {
  web_power_save::State state;
  state.reset(0);
  ASSERT_TRUE(state.shouldEnterModemSleep(15000, false, false));

  // First request wakes radio.
  EXPECT_TRUE(state.noteActivity(16000));
  EXPECT_FALSE(state.modemSleep);

  // Second request while already awake does not re-issue setSleep.
  EXPECT_FALSE(state.noteActivity(17000));

  // Idle timer runs from 17000, not 0.
  EXPECT_FALSE(state.shouldEnterModemSleep(31999, false, false));
  EXPECT_TRUE(state.shouldEnterModemSleep(32000, false, false));
}

TEST(WebPowerSavePolicy, NeverEntersModemSleepInApModeOrWhileBusy) {
  web_power_save::State state;
  state.reset(0);

  EXPECT_FALSE(state.shouldEnterModemSleep(60000, /*apMode=*/true, /*busy=*/false));
  EXPECT_FALSE(state.modemSleep);

  EXPECT_FALSE(state.shouldEnterModemSleep(60000, /*apMode=*/false, /*busy=*/true));
  EXPECT_FALSE(state.modemSleep);

  // Once the transfer ends without recent traffic, modem sleep can engage.
  EXPECT_TRUE(state.shouldEnterModemSleep(60000, /*apMode=*/false, /*busy=*/false));
}

TEST(WebPowerSavePolicy, HandlesMillisWraparound) {
  web_power_save::State state;
  state.reset(0xFFFFF000u);

  // 4096 + 10903 = 14999 ms elapsed across uint32_t wrap.
  EXPECT_FALSE(state.shouldEnterModemSleep(10903u, false, false));
  EXPECT_TRUE(state.shouldEnterModemSleep(10904u, false, false));
}

}  // namespace
