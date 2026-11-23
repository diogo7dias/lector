#include <gtest/gtest.h>

#include "activities/boot_sleep/SleepFacePaint.h"
#include "activities/boot_sleep/SleepPreClear.h"

namespace {

DeviceProfile uc8253X3() {
  DeviceProfile d;
  d.isX3 = true;
  d.controllerIsUc8279 = true;
  return d;
}

DeviceProfile uc8279X4() {
  DeviceProfile d;
  d.controllerIsUc8279 = true;
  return d;
}

DeviceProfile ssd1677X4Pro() {
  DeviceProfile d;
  d.isX4Pro = true;
  return d;
}

}  // namespace

// Match CrossPoint: one white HALF_REFRESH pre-clear on every board.
TEST(SleepPreClear, RunsOnEveryBoard) {
  EXPECT_EQ(sleepPreClearSubmissions(uc8253X3()), 1);
  EXPECT_EQ(sleepPreClearSubmissions(ssd1677X4Pro()), 1);
  EXPECT_EQ(sleepPreClearSubmissions(uc8279X4()), 1);
}

// End-to-end wallpaper lock cost: popup (1) + white pre-clear (1) + grayscale base (1) +
// grayscale planes (1) = 4 submissions on every board.
TEST(SleepPreClear, AWallpaperLockCostsFourSubmissionsOnEveryBoard) {
  for (const DeviceProfile& dev : {uc8253X3(), ssd1677X4Pro(), uc8279X4()}) {
    const int total = sleepPreClearSubmissions(dev) +
                      sleep_face::totalSubmissions(sleep_face::Face::Wallpaper, /*sourceHasGrayscale=*/true, dev);
    EXPECT_EQ(total, 4);
  }
}
