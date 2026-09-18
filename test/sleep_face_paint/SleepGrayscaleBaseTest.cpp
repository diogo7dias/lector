// The grayscale sleep base waveform, as a value test rather than a source-text grep.
//
// This function used to read two globals (`gpio` and `BoardConfig::ACTIVE`), which made
// it uncallable from a host test: the only way to pin its rule was to read the .cpp back
// as text and look for the spelling of the guard. It now takes a DeviceProfile parameter,
// so the rule is asserted on behaviour.
//
// Every expectation below states TODAY'S shipped behaviour. A waveform is physically
// observable on an e-ink panel, and retained charge means a change cannot be verified by
// reading code, so nothing here is an opinion about what the waveform should be.
#include <gtest/gtest.h>

#include "activities/boot_sleep/SleepGrayscaleBase.h"

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

// The real X4 Pro: it probes as a UC8279 (XTDET promotes SSD1677 -> UC8279 at boot).
DeviceProfile uc8279X4Pro() {
  DeviceProfile d;
  d.isX4Pro = true;
  d.hasTouch = true;
  d.controllerIsUc8279 = true;
  return d;
}

}  // namespace

// CONTRADICTION SETTLED ON HARDWARE.
//
// SleepActivity.cpp said the base "Must stay HALF: the gray nudge LUT is calibrated
// against the pixel state the single-pass HALF waveform leaves behind. A FULL (GC) base
// parks pixels in a different charge state and the differential nudge then lands
// unevenly." The code nevertheless returned FULL for a UC8279 non-X3 board, which is what
// the X4 Pro actually is — the earlier reading that it was an SSD1677 was wrong.
//
// Judged on an X4 Pro: with a FULL base the wallpaper lost its mid tones (black, white and
// one grey) and the status bar kept a ghost. HALF restores both. The prose was right.
//
// The ghost has a second mechanism worth recording: Uc8279X4Driver::displayGrayscaleBase
// only takes its real B/W activation branch — the periodic ghost purge — when the fallback
// is Half. A FULL base skipped that purge on every sleep.
TEST(SleepGrayscaleBase, AUc8279NonX3BaseIsHalf) {
  EXPECT_EQ(sleepGrayscaleBaseRefresh(uc8279X4()), HalDisplay::HALF_REFRESH);
}

// The X3's base is the exact waveform its gray-nudge LUT was calibrated against.
TEST(SleepGrayscaleBase, AUc8279X3BaseIsHalf) {
  EXPECT_EQ(sleepGrayscaleBaseRefresh(uc8279X3()), HalDisplay::HALF_REFRESH);
}

// The X4 Pro takes HALF, on the controller it really reports.
TEST(SleepGrayscaleBase, TheX4ProBaseIsHalf) {
  EXPECT_EQ(sleepGrayscaleBaseRefresh(uc8279X4Pro()), HalDisplay::HALF_REFRESH);
}

// No board takes FULL: every device's gray nudge is calibrated against the HALF base.
TEST(SleepGrayscaleBase, ANonUc8279BoardTakesHalf) {
  DeviceProfile d;
  d.controllerIsUc8279 = false;
  EXPECT_EQ(sleepGrayscaleBaseRefresh(d), HalDisplay::HALF_REFRESH);
}
