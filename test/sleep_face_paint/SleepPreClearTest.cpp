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

// The scrub runs on every board. It used to skip the X3 on the premise that the
// requestResync() before its grayscale base already cleared the panel; it does not.
// requestResync() forces the full-sync branch, which paints the frame against a baseline
// forced white, and with that baseline only the pixels whose target is white take
// lut_x3_ww_full's excursion to the black rail. Every other pixel is driven straight to
// its target and keeps the charge underneath it — which on a grayscale wallpaper is the
// whole mid-tone area, because the BW base paints every level except pure white as black.
// That was the X3 lock-screen ghost.
TEST(SleepPreClear, RunsOnEveryBoard) {
  EXPECT_GT(sleepPreClearSubmissions(uc8253X3()), 0);
  EXPECT_GT(sleepPreClearSubmissions(ssd1677X4Pro()), 0);
  EXPECT_GT(sleepPreClearSubmissions(uc8279X4()), 0);
}

// One submission on the X3, because the black excursion is inside its own full bank: an
// all-white target against the forced white baseline puts every pixel in WW, and
// lut_x3_ww_full runs 24 frames to black before 24 back to white.
TEST(SleepPreClear, TheX3SpendsOneSubmission) { EXPECT_EQ(sleepPreClearSubmissions(uc8253X3()), 1); }

// Four elsewhere: black, white, twice. Those boards have no pass that inverts a pixel
// which is not changing, so the frames have to be drawn. One cycle left the X4 Pro's
// status bar still faintly visible on the bench.
TEST(SleepPreClear, TheOtherBoardsSpendFour) {
  EXPECT_EQ(sleepPreClearSubmissions(ssd1677X4Pro()), 4);
  EXPECT_EQ(sleepPreClearSubmissions(uc8279X4()), 4);
}

// What one X3 lock costs the panel, end to end, for the priority face: the "Entering
// sleep" popup, the scrub, then the wallpaper's black-and-white base and its two
// grayscale planes. Four. The scrub is the one this fix added; every extra submission on
// this path is a visible flash at lock, so this number is pinned rather than left to
// drift.
TEST(SleepPreClear, AnX3WallpaperLockCostsFourSubmissions) {
  const DeviceProfile dev = uc8253X3();
  const int total = sleepPreClearSubmissions(dev) +
                    sleep_face::totalSubmissions(sleep_face::Face::Wallpaper, /*sourceHasGrayscale=*/true, dev);
  EXPECT_EQ(total, 4);
}

// The X4 Pro is unchanged by the X3 fix: seven, exactly as it shipped.
TEST(SleepPreClear, TheX4ProWallpaperLockIsUnchanged) {
  const DeviceProfile dev = ssd1677X4Pro();
  const int total = sleepPreClearSubmissions(dev) +
                    sleep_face::totalSubmissions(sleep_face::Face::Wallpaper, /*sourceHasGrayscale=*/true, dev);
  EXPECT_EQ(total, 7);
}
