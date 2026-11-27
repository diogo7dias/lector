#include <gtest/gtest.h>

#include "SilentRestart.h"

// KOReader Sync switches the radio off after an upload (WiFi.mode(WIFI_OFF)), so by
// onExit the mode reads WIFI_MODE_NULL. The heap the Wi-Fi and TLS session fragmented
// is still fragmented, so the screen must reboot on the way out all the same.
TEST(SilentRestart, AScreenThatUsedWifiRebootsWithTheRadioOff) { EXPECT_TRUE(wifiRestartNeeded(false, true)); }

TEST(SilentRestart, ARadioStillOnReboots) { EXPECT_TRUE(wifiRestartNeeded(true, false)); }

// Backing out before Wi-Fi ever came up has nothing to give back.
TEST(SilentRestart, NoWifiNoReboot) { EXPECT_FALSE(wifiRestartNeeded(false, false)); }
