// Host proof of the two-action button: hold fires at the threshold with the button
// still down, and the release that ends it is not also a short press.

#include <gtest/gtest.h>

#include "util/HoldButtonPolicy.h"

using hold_button::Fired;
using hold_button::Tracker;

namespace {
constexpr unsigned long kThreshold = 1000;
}

// Let go before the threshold: the short action, and only on the release, because
// until then the firmware cannot know which of the two the user meant.
TEST(HoldButton, ShortPressFiresOnRelease) {
  Tracker t;
  EXPECT_EQ(Fired::None, t.update(/*isDown=*/true, /*wasReleasedNow=*/false, 200, kThreshold));
  EXPECT_EQ(Fired::Short, t.update(/*isDown=*/false, /*wasReleasedNow=*/true, 0, kThreshold));
}

// The whole point: the hold acts while the finger is still down.
TEST(HoldButton, HoldFiresAtTheThresholdWithoutWaitingForRelease) {
  Tracker t;
  EXPECT_EQ(Fired::None, t.update(true, false, 999, kThreshold));
  EXPECT_EQ(Fired::Hold, t.update(true, false, 1000, kThreshold));
}

// Keeping it down after that must not repeat the action.
TEST(HoldButton, HoldFiresOnlyOncePerPress) {
  Tracker t;
  ASSERT_EQ(Fired::Hold, t.update(true, false, 1000, kThreshold));
  EXPECT_EQ(Fired::None, t.update(true, false, 1500, kThreshold));
  EXPECT_EQ(Fired::None, t.update(true, false, 4000, kThreshold));
}

// The release after a hold is the tail of an action already taken. Reporting it as a
// short press is how holding to delete would also open the file underneath.
TEST(HoldButton, ReleaseAfterAHoldIsNotAShortPress) {
  Tracker t;
  ASSERT_EQ(Fired::Hold, t.update(true, false, 1000, kThreshold));
  EXPECT_EQ(Fired::None, t.update(false, true, 0, kThreshold));
}

// And the next press starts clean, so a hold then a tap gives hold then short.
TEST(HoldButton, NextPressStartsClean) {
  Tracker t;
  ASSERT_EQ(Fired::Hold, t.update(true, false, 1000, kThreshold));
  ASSERT_EQ(Fired::None, t.update(false, true, 0, kThreshold));
  EXPECT_EQ(Fired::None, t.update(true, false, 100, kThreshold));
  EXPECT_EQ(Fired::Short, t.update(false, true, 0, kThreshold));
}

// A pass with the button up and no edge is silence, not a short press.
TEST(HoldButton, IdlePassesFireNothing) {
  Tracker t;
  EXPECT_EQ(Fired::None, t.update(false, false, 0, kThreshold));
  EXPECT_EQ(Fired::None, t.update(false, false, 0, kThreshold));
}

// A button with nothing bound to a hold has no ambiguity to resolve, so it acts the
// instant it goes down. That is the whole point of the sweep: no waiting for a
// release the firmware never needed.
TEST(HoldButton, WithoutAHoldActionTheShortOneFiresOnThePress) {
  Tracker t;
  EXPECT_EQ(Fired::Short, t.updatePressOnly(/*wasPressedNow=*/true));
  EXPECT_EQ(Fired::None, t.updatePressOnly(/*wasPressedNow=*/false));
}
