#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <utility>

#include "activities/boot_sleep/SleepPreClear.h"

// Execute the production pre-clear. These are renderer calls, not driver refresh
// commands: an X3 FULL additionally fires an internal FAST settle waveform.
TEST(SleepPreClear, SubmitsTheExpectedFullScreenTargets) {
  for (const bool x3 : {false, true}) {
    for (const bool landscape : {false, true}) {
      DeviceProfile dev;
      dev.isX3 = x3;
      GfxRenderer renderer;
      if (landscape) std::swap(renderer.width, renderer.height);
      sleepPreClear(renderer, dev);
      const std::vector<uint8_t> expected = x3 ? std::vector<uint8_t>{0xFF}
                                             : std::vector<uint8_t>{0, 0xFF, 0, 0xFF};
      EXPECT_EQ(renderer.targets, expected);
    }
  }
}
