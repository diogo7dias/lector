#include <gtest/gtest.h>

#include "activities/boot_sleep/SleepPreClear.h"

namespace {

DeviceProfile uc8279X3() {
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

// The X3's grayscale base already clears the panel for itself: HalDisplay.cpp:290 calls
// requestResync() before the base pass. Scrubbing first would spend a second of panel time
// solving a problem this board does not have.
TEST(SleepPreClear, TheX3DoesNotNeedIt) { EXPECT_FALSE(sleepNeedsPreClear(uc8279X3())); }

// The X4 Pro's SSD1677 implements no requestResync(), so the wallpaper is painted straight
// over whatever the reader left in the ink. Without this scrub the status bar ghosts through
// the lock screen — the reported symptom that put this code here.
TEST(SleepPreClear, TheX4ProNeedsIt) { EXPECT_TRUE(sleepNeedsPreClear(ssd1677X4Pro())); }

// The rule is "cannot resync", not "is the X4 Pro": the C3 X4 has no resync path either.
TEST(SleepPreClear, TheC3X4NeedsIt) { EXPECT_TRUE(sleepNeedsPreClear(uc8279X4())); }
