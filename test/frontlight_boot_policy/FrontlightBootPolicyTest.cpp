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

TEST(FrontlightBootPolicy, ScheduledWarmthInterpolatesAcrossSunsetAndCoolsAtDawn) {
  // Schedule 20:00 (1200) -> 07:00 (420), base warmth 20 -> peak 100.
  EXPECT_EQ(frontlight::scheduledWarmth(1200, 420, 1199, 20), 20);
  EXPECT_EQ(frontlight::scheduledWarmth(1200, 420, 1200, 20), 20);
  // Halfway through the 90-minute evening ramp (20:45 = 1245) -> 60.
  EXPECT_EQ(frontlight::scheduledWarmth(1200, 420, 1245, 20), 60);
  // Night plateau (23:00 = 1380 and 03:00 = 180) -> peak 100.
  EXPECT_EQ(frontlight::scheduledWarmth(1200, 420, 1380, 20), 100);
  EXPECT_EQ(frontlight::scheduledWarmth(1200, 420, 180, 20), 100);
  // Halfway through 30-minute dawn cooldown (06:45 = 405) -> 60.
  EXPECT_EQ(frontlight::scheduledWarmth(1200, 420, 405, 20), 60);
  EXPECT_EQ(frontlight::scheduledWarmth(1200, 420, 420, 20), 20);
}

TEST(FrontlightBootPolicy, CandleFadeDimsAndWarmsMonotonicallyThenStops) {
  const auto start = frontlight::candleFadeStep(30, 40, 0);
  EXPECT_FALSE(start.done);
  EXPECT_EQ(start.brightness, 30);
  EXPECT_EQ(start.warmth, 40);

  const auto mid = frontlight::candleFadeStep(30, 40, frontlight::CANDLE_FADE_DURATION_MS / 2);
  EXPECT_FALSE(mid.done);
  EXPECT_EQ(mid.brightness, 15);
  EXPECT_EQ(mid.warmth, 70);

  const auto end = frontlight::candleFadeStep(30, 40, frontlight::CANDLE_FADE_DURATION_MS);
  EXPECT_TRUE(end.done);
  EXPECT_EQ(end.brightness, 0);
  EXPECT_EQ(end.warmth, 100);
}

TEST(FrontlightBootPolicy, ActiveLightProfileSeparatesDayAndNightPairs) {
  const auto day = frontlight::activeLightProfile(false, 25, 30, 10, 85);
  EXPECT_EQ(day.brightness, 25);
  EXPECT_EQ(day.warmth, 30);

  const auto night = frontlight::activeLightProfile(true, 25, 30, 10, 85);
  EXPECT_EQ(night.brightness, 10);
  EXPECT_EQ(night.warmth, 85);
}
