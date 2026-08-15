#include <gtest/gtest.h>

#include "frontlight/FrontlightBootPolicy.h"

using frontlight::BootContext;
using frontlight::restoreLightOnAtBoot;

TEST(FrontlightBootPolicy, SavedOffStaysOff) {
  EXPECT_FALSE(restoreLightOnAtBoot(BootContext{false, true, false}));
  EXPECT_FALSE(restoreLightOnAtBoot(BootContext{false, true, true}));
}

TEST(FrontlightBootPolicy, RestoreOnWakeBringsTheLightBack) {
  EXPECT_TRUE(restoreLightOnAtBoot(BootContext{true, true, false}));
}

TEST(FrontlightBootPolicy, WakeStartsDarkWhenRestoreIsOff) {
  EXPECT_FALSE(restoreLightOnAtBoot(BootContext{true, false, false}));
}

// A silent maintenance reboot (heap defrag) is invisible to the reader: the
// light was lit a moment ago and must not go dark under their hands.
TEST(FrontlightBootPolicy, SilentRebootKeepsTheLightRegardlessOfRestoreSetting) {
  EXPECT_TRUE(restoreLightOnAtBoot(BootContext{true, false, true}));
}
