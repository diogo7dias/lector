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

TEST(FrontlightBootPolicy, ScheduleLightsAWakeWhateverWasSaved) {
  EXPECT_TRUE(restoreLightOnAtBoot(BootContext{false, false, false, false, true}));
  EXPECT_TRUE(restoreLightOnAtBoot(BootContext{true, false, false, false, true}));
}

TEST(FrontlightBootPolicy, OutsideTheScheduleRestoreStillApplies) {
  EXPECT_TRUE(restoreLightOnAtBoot(BootContext{true, true, false, false, false}));
  EXPECT_FALSE(restoreLightOnAtBoot(BootContext{false, true, false, false, false}));
}

// The reader is mid-session: a maintenance restart must not switch the light for the schedule.
TEST(FrontlightBootPolicy, SilentRebootIgnoresTheSchedule) {
  EXPECT_FALSE(restoreLightOnAtBoot(BootContext{false, false, true, false, true}));
}

TEST(FrontlightBootPolicy, ScheduleWithinADay) {
  EXPECT_FALSE(frontlight::insideSchedule(480, 1020, 479));
  EXPECT_TRUE(frontlight::insideSchedule(480, 1020, 480));
  EXPECT_TRUE(frontlight::insideSchedule(480, 1020, 1019));
  EXPECT_FALSE(frontlight::insideSchedule(480, 1020, 1020));
}

TEST(FrontlightBootPolicy, ScheduleAcrossMidnight) {
  EXPECT_TRUE(frontlight::insideSchedule(1200, 420, 1200));
  EXPECT_TRUE(frontlight::insideSchedule(1200, 420, 0));
  EXPECT_TRUE(frontlight::insideSchedule(1200, 420, 419));
  EXPECT_FALSE(frontlight::insideSchedule(1200, 420, 420));
  EXPECT_FALSE(frontlight::insideSchedule(1200, 420, 1199));
}

TEST(FrontlightBootPolicy, ScheduleWithEqualEndsIsEmpty) {
  EXPECT_FALSE(frontlight::insideSchedule(600, 600, 600));
  EXPECT_FALSE(frontlight::insideSchedule(600, 600, 0));
}
