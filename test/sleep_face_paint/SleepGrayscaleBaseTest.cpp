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

DeviceProfile ssd1677X4Pro() {
  DeviceProfile d;
  d.isX4Pro = true;
  d.hasTouch = true;
  d.controllerIsUc8279 = false;
  return d;
}

}  // namespace

// CONTRADICTION ON RECORD, for Diogo to settle on hardware — do not "fix" it from here.
//
// SleepActivity.cpp:531-534 says of the call one line above it: "Must stay HALF: the gray
// nudge LUT is calibrated against the pixel state the single-pass HALF waveform leaves
// behind. A FULL (GC) base parks pixels in a different charge state and the differential
// nudge then lands unevenly (blotchy noise in gray areas)." SleepGrayscaleBase.h then
// returns FULL for exactly that board. The header's own prose ("The X4 and X3 keep HALF")
// disagrees with its code too.
//
// The test asserts the CODE, because the code is what ships and what the panel has been
// wearing. Resolving which of the two is right needs an X4 ghosting check on real
// hardware: the difference is blotchy grey in the wallpaper, and retained e-ink charge
// means it cannot be read off a source file.
TEST(SleepGrayscaleBase, AUc8279NonX3BaseIsFullToday) {
  EXPECT_EQ(sleepGrayscaleBaseRefresh(uc8279X4()), HalDisplay::FULL_REFRESH);
}

// The X3's base is the exact waveform its gray-nudge LUT was calibrated against.
TEST(SleepGrayscaleBase, AUc8279X3BaseIsHalf) {
  EXPECT_EQ(sleepGrayscaleBaseRefresh(uc8279X3()), HalDisplay::HALF_REFRESH);
}

TEST(SleepGrayscaleBase, TheX4ProBaseIsHalf) {
  EXPECT_EQ(sleepGrayscaleBaseRefresh(ssd1677X4Pro()), HalDisplay::HALF_REFRESH);
}

// The rule is about the CONTROLLER as well as the board: a non-UC8279 board takes HALF
// whatever else it is, which is why the profile carries both fields.
TEST(SleepGrayscaleBase, ANonUc8279BoardTakesHalf) {
  DeviceProfile d;
  d.controllerIsUc8279 = false;
  EXPECT_EQ(sleepGrayscaleBaseRefresh(d), HalDisplay::HALF_REFRESH);
}
