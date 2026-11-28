#include <gtest/gtest.h>

#include "IdlePrewarmTiming.h"

namespace {

using idle_prewarm::CLOCK_DROP_MARGIN_MS;
using idle_prewarm::isReady;
using idle_prewarm::RENDER_DEBOUNCE_MS;

// HalPowerManager::IDLE_POWER_SAVING_MS per board.
constexpr unsigned long X4PRO_DROP_MS = 1000;
constexpr unsigned long C3_DROP_MS = 3000;

TEST(IdlePrewarmTiming, NotReadyBeforeFirstRender) {
  EXPECT_FALSE(isReady(/*nowMs=*/2000, /*lastInputMs=*/0, /*lastRenderCompleteMs=*/0, X4PRO_DROP_MS));
  EXPECT_FALSE(isReady(9000, 0, 0, C3_DROP_MS));
}

TEST(IdlePrewarmTiming, X4ProStartsBeforeTheClockDrop) {
  // Fast turn: input at 1000, render done at 1450. The debounce alone would end at 1850,
  // and a slower turn's past the drop at 2000.
  EXPECT_FALSE(isReady(1460, 1000, 1450, X4PRO_DROP_MS));
  EXPECT_FALSE(isReady(1649, 1000, 1450, X4PRO_DROP_MS));
  EXPECT_TRUE(isReady(1650, 1000, 1450, X4PRO_DROP_MS));
  // Slow turn: render done at 1950. Ready on the next pass, before the drop at 2000.
  EXPECT_TRUE(isReady(1960, 1000, 1950, X4PRO_DROP_MS));
}

TEST(IdlePrewarmTiming, X4ProQuickRenderStillWaitsOutTheDebounce) {
  // Render done 100 ms after the input: the debounce ends at 1501, before the margin at 1650.
  EXPECT_FALSE(isReady(1500, 1000, 1100, X4PRO_DROP_MS));
  EXPECT_TRUE(isReady(1501, 1000, 1100, X4PRO_DROP_MS));
}

TEST(IdlePrewarmTiming, C3KeepsTheDebounceAfterTheRender) {
  // Input at 1000, render done at 1900 (a C3 page turn). The drop is at 4000, so nothing
  // is gained by starting early: the work waits the 400 ms a page-flipper needs.
  EXPECT_FALSE(isReady(1910, 1000, 1900, C3_DROP_MS));
  EXPECT_FALSE(isReady(1900 + RENDER_DEBOUNCE_MS, 1000, 1900, C3_DROP_MS));
  EXPECT_TRUE(isReady(1901 + RENDER_DEBOUNCE_MS, 1000, 1900, C3_DROP_MS));
  // A render so slow that the debounce would cross the drop starts at the margin instead.
  EXPECT_TRUE(isReady(1000 + C3_DROP_MS - CLOCK_DROP_MARGIN_MS, 1000, 3600, C3_DROP_MS));
}

TEST(IdlePrewarmTiming, MarginLargerThanTheDropNeverFiresEarly) {
  EXPECT_FALSE(isReady(1010, 1000, 1005, CLOCK_DROP_MARGIN_MS));
  EXPECT_TRUE(isReady(1406, 1000, 1005, CLOCK_DROP_MARGIN_MS));
}

TEST(IdlePrewarmTiming, HandlesUnsignedMillisWraparound) {
  const unsigned long lastInput = static_cast<unsigned long>(-200);
  const unsigned long renderDone = lastInput + 300;
  EXPECT_FALSE(isReady(lastInput + 649, lastInput, renderDone, X4PRO_DROP_MS));
  EXPECT_TRUE(isReady(lastInput + 650, lastInput, renderDone, X4PRO_DROP_MS));
}

}  // namespace
