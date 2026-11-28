#include <gtest/gtest.h>

#include "IdlePrewarmTiming.h"

namespace {

using idle_prewarm::DELAY_AFTER_INPUT_MS;
using idle_prewarm::isReady;

TEST(IdlePrewarmTiming, DelayFitsInsideX4ProIdleClockDropWindow) {
  EXPECT_GE(DELAY_AFTER_INPUT_MS, 500UL);
  EXPECT_LE(DELAY_AFTER_INPUT_MS, 800UL);
  EXPECT_LT(DELAY_AFTER_INPUT_MS, 1000UL);  // HalPowerManager::IDLE_POWER_SAVING_MS on X4 Pro
}

TEST(IdlePrewarmTiming, NotReadyBeforeFirstRender) {
  EXPECT_FALSE(isReady(/*nowMs=*/2000, /*lastInputMs=*/0, /*lastRenderCompleteMs=*/0));
}

TEST(IdlePrewarmTiming, WaitsPastRapidFlipThenRunsBeforeClockDrop) {
  // Fast turn: input at 1000, render finishes at 1450.
  EXPECT_FALSE(isReady(1460, 1000, 1450));
  EXPECT_FALSE(isReady(1649, 1000, 1450));
  // Fires at 1650 (650 ms after input), 350 ms before the 1000 ms clock drop.
  EXPECT_TRUE(isReady(1650, 1000, 1450));
}

TEST(IdlePrewarmTiming, SlowRenderRunsImmediatelyAfterRenderBeforeClockDrop) {
  // Slow/clean turn: input at 1000, render finishes at 1950 (> 650 ms).
  // Must fire on the very next loop pass at 1960, before main.cpp drops to 40 MHz at 2000.
  EXPECT_TRUE(isReady(1960, 1000, 1950));
}

TEST(IdlePrewarmTiming, HandlesUnsignedMillisWraparound) {
  const unsigned long lastInput = static_cast<unsigned long>(-200);
  const unsigned long renderDone = lastInput + 100;
  EXPECT_FALSE(isReady(lastInput + 649, lastInput, renderDone));
  EXPECT_TRUE(isReady(lastInput + 650, lastInput, renderDone));
}

}  // namespace
