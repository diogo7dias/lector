#include <gtest/gtest.h>

#include "frontlight/FrontlightBootPolicy.h"

using frontlight::BootContext;
using frontlight::restoreLightOnAtBoot;

TEST(FrontlightBootPolicy, SavedOffStaysOffOnWake) {
  EXPECT_FALSE(restoreLightOnAtBoot(BootContext{false, true, false, false}));
}

TEST(FrontlightBootPolicy, RestoreOnWakeBringsTheLightBack) {
  EXPECT_TRUE(restoreLightOnAtBoot(BootContext{true, true, false, false}));
}

TEST(FrontlightBootPolicy, WakeStartsDarkWhenRestoreIsOff) {
  EXPECT_FALSE(restoreLightOnAtBoot(BootContext{true, false, false, false}));
}

// A silent maintenance reboot (heap defrag) is invisible to the reader: the
// light was lit a moment ago and must not go dark under their hands.
TEST(FrontlightBootPolicy, SilentRebootKeepsALitLight) {
  EXPECT_TRUE(restoreLightOnAtBoot(BootContext{true, false, true, true}));
  EXPECT_TRUE(restoreLightOnAtBoot(BootContext{false, false, true, true}));
}

// Woken with Restore Light on Wake off, the light stays dark while "was on"
// stays saved. A silent reboot must not switch it on from that saved state.
TEST(FrontlightBootPolicy, SilentRebootKeepsADarkLight) {
  EXPECT_FALSE(restoreLightOnAtBoot(BootContext{true, false, true, false}));
  EXPECT_FALSE(restoreLightOnAtBoot(BootContext{true, true, true, false}));
}
