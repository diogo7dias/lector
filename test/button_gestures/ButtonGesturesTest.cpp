#include <gtest/gtest.h>

#include "util/ButtonGestures.h"

using button_gestures::Detector;
using button_gestures::Event;

namespace {

// A detector for a button with every gesture bound, unless a test says otherwise.
Detector armed(const bool hasDouble = true, const bool hasHold = true) {
  Detector d;
  d.configure(hasDouble, hasHold);
  return d;
}

TEST(ButtonGestures, APlainTapIsASingleClick) {
  Detector d = armed(/*hasDouble=*/false, /*hasHold=*/false);
  EXPECT_EQ(d.onPress(0), Event::None);
  EXPECT_EQ(d.onRelease(80), Event::Single);
}

TEST(ButtonGestures, WithNoDoubleBoundTheSingleFiresOnRelease) {
  // The whole point of arming the wait only where it is asked for: a page turn
  // must not wait to find out whether a second press is coming.
  Detector d = armed(/*hasDouble=*/false, /*hasHold=*/true);
  d.onPress(0);
  EXPECT_EQ(d.onRelease(60), Event::Single);
  EXPECT_EQ(d.tick(60 + button_gestures::DOUBLE_WINDOW_MS + 1), Event::None);
}

TEST(ButtonGestures, WithDoubleBoundTheSingleWaitsForTheWindow) {
  Detector d = armed();
  d.onPress(0);
  EXPECT_EQ(d.onRelease(60), Event::None);
  EXPECT_EQ(d.tick(60 + button_gestures::DOUBLE_WINDOW_MS - 1), Event::None);
  EXPECT_EQ(d.tick(60 + button_gestures::DOUBLE_WINDOW_MS), Event::Single);
}

TEST(ButtonGestures, ASecondPressInsideTheWindowIsADoubleClick) {
  Detector d = armed();
  d.onPress(0);
  d.onRelease(60);
  EXPECT_EQ(d.onPress(60 + button_gestures::DOUBLE_WINDOW_MS - 10), Event::Double);
}

TEST(ButtonGestures, TheDoubleIsReportedOnceAndTheSecondReleaseIsQuiet) {
  Detector d = armed();
  d.onPress(0);
  d.onRelease(60);
  ASSERT_EQ(d.onPress(100), Event::Double);
  EXPECT_EQ(d.onRelease(160), Event::None);
  EXPECT_EQ(d.tick(1000), Event::None);
}

TEST(ButtonGestures, ASecondPressAfterTheWindowIsANewSingle) {
  Detector d = armed();
  d.onPress(0);
  d.onRelease(60);
  ASSERT_EQ(d.tick(60 + button_gestures::DOUBLE_WINDOW_MS), Event::Single);
  EXPECT_EQ(d.onPress(500), Event::None);
  EXPECT_EQ(d.onRelease(560), Event::None);
  EXPECT_EQ(d.tick(560 + button_gestures::DOUBLE_WINDOW_MS), Event::Single);
}

TEST(ButtonGestures, HoldFiresWhileTheButtonIsStillDown) {
  Detector d = armed();
  d.onPress(0);
  EXPECT_EQ(d.tick(button_gestures::HOLD_MS - 1), Event::None);
  EXPECT_EQ(d.tick(button_gestures::HOLD_MS), Event::Hold);
}

TEST(ButtonGestures, AHoldFiresOnlyOnceForOnePress) {
  Detector d = armed();
  d.onPress(0);
  ASSERT_EQ(d.tick(button_gestures::HOLD_MS), Event::Hold);
  EXPECT_EQ(d.tick(button_gestures::HOLD_MS + 500), Event::None);
}

TEST(ButtonGestures, AHoldSwallowsTheReleaseSoItIsNotAlsoASingle) {
  Detector d = armed();
  d.onPress(0);
  ASSERT_EQ(d.tick(button_gestures::HOLD_MS), Event::Hold);
  EXPECT_EQ(d.onRelease(button_gestures::HOLD_MS + 200), Event::None);
  EXPECT_EQ(d.tick(button_gestures::HOLD_MS + 2000), Event::None);
}

TEST(ButtonGestures, WithNoHoldBoundALongPressIsStillASingle) {
  // Nothing bound to hold means the firmware's own hold behaviour (page repeat,
  // list paging) owns the press; the detector must not invent a gesture there.
  Detector d = armed(/*hasDouble=*/false, /*hasHold=*/false);
  d.onPress(0);
  EXPECT_EQ(d.tick(button_gestures::HOLD_MS * 3), Event::None);
  EXPECT_EQ(d.onRelease(button_gestures::HOLD_MS * 3 + 10), Event::Single);
}

TEST(ButtonGestures, AnUnboundButtonReportsNothingAtAll) {
  // Not configured: every gesture falls through to the firmware's own handling.
  Detector d;
  d.configure(false, false);
  d.setSingleBound(false);
  d.onPress(0);
  EXPECT_EQ(d.onRelease(60), Event::None);
}

TEST(ButtonGestures, AWrappedMillisCounterDoesNotStrandTheDetector) {
  // millis() wraps after 49 days; a press across the wrap must not sit forever
  // waiting for a window that can never close.
  Detector d = armed();
  d.onPress(0xFFFFFF00);
  EXPECT_EQ(d.onRelease(0xFFFFFF40), Event::None);
  // 0xFFFFFF40 to 0x00000100 is 448 ms across the wrap, past the window.
  EXPECT_EQ(d.tick(0x00000100), Event::Single);
}

}  // namespace
