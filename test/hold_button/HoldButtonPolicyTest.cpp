// Host proof of the two-action button: hold fires at the threshold with the button
// still down, the release that ends it is not also a short press, and a release whose
// press this tracker never saw is not an action at all.

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
  EXPECT_EQ(Fired::None, t.update(/*wasPressedNow=*/true, /*isDown=*/true, /*wasReleasedNow=*/false, 0, kThreshold));
  EXPECT_EQ(Fired::None, t.update(false, true, false, 200, kThreshold));
  EXPECT_EQ(Fired::Short, t.update(false, /*isDown=*/false, /*wasReleasedNow=*/true, 0, kThreshold));
}

// The whole point: the hold acts while the finger is still down.
TEST(HoldButton, HoldFiresAtTheThresholdWithoutWaitingForRelease) {
  Tracker t;
  EXPECT_EQ(Fired::None, t.update(true, true, false, 0, kThreshold));
  EXPECT_EQ(Fired::None, t.update(false, true, false, 999, kThreshold));
  EXPECT_EQ(Fired::Hold, t.update(false, true, false, 1000, kThreshold));
}

// Keeping it down after that must not repeat the action.
TEST(HoldButton, HoldFiresOnlyOncePerPress) {
  Tracker t;
  ASSERT_EQ(Fired::Hold, t.update(true, true, false, 1000, kThreshold));
  EXPECT_EQ(Fired::None, t.update(false, true, false, 1500, kThreshold));
  EXPECT_EQ(Fired::None, t.update(false, true, false, 4000, kThreshold));
}

// The release after a hold is the tail of an action already taken. Reporting it as a
// short press is how holding to delete would also open the file underneath.
TEST(HoldButton, ReleaseAfterAHoldIsNotAShortPress) {
  Tracker t;
  ASSERT_EQ(Fired::Hold, t.update(true, true, false, 1000, kThreshold));
  EXPECT_EQ(Fired::None, t.update(false, false, true, 0, kThreshold));
}

// And the next press starts clean, so a hold then a tap gives hold then short.
TEST(HoldButton, NextPressStartsClean) {
  Tracker t;
  ASSERT_EQ(Fired::Hold, t.update(true, true, false, 1000, kThreshold));
  ASSERT_EQ(Fired::None, t.update(false, false, true, 0, kThreshold));
  EXPECT_EQ(Fired::None, t.update(true, true, false, 0, kThreshold));
  EXPECT_EQ(Fired::None, t.update(false, true, false, 100, kThreshold));
  EXPECT_EQ(Fired::Short, t.update(false, false, true, 0, kThreshold));
}

// A pass with the button up and no edge is silence, not a short press.
TEST(HoldButton, IdlePassesFireNothing) {
  Tracker t;
  EXPECT_EQ(Fired::None, t.update(false, false, false, 0, kThreshold));
  EXPECT_EQ(Fired::None, t.update(false, false, false, 0, kThreshold));
}

// The bug this arming rule exists for. The screen that opens is created inside the
// pass that handled the press, so its first pass sees the button already down and
// then the release. Acting on that release opens whatever this screen happens to
// have selected, which is how opening the file browser also opened its search box.
TEST(HoldButton, AReleaseWhosePressWasNeverSeenIsNotAShortPress) {
  Tracker t;
  EXPECT_EQ(Fired::None, t.update(/*wasPressedNow=*/false, /*isDown=*/true, false, 40, kThreshold));
  EXPECT_EQ(Fired::None, t.update(false, false, /*wasReleasedNow=*/true, 0, kThreshold));
}

// Same for the hold half: a button already down when the screen opened must not reach
// the threshold on this screen's watch either.
TEST(HoldButton, AHoldInheritedFromAnotherScreenNeverFires) {
  Tracker t;
  EXPECT_EQ(Fired::None, t.update(false, true, false, 4000, kThreshold));
  EXPECT_EQ(Fired::None, t.update(false, false, true, 0, kThreshold));
}

// And the inherited press does not poison the next real one.
TEST(HoldButton, ARealPressAfterAnInheritedOneStillWorks) {
  Tracker t;
  ASSERT_EQ(Fired::None, t.update(false, true, false, 40, kThreshold));
  ASSERT_EQ(Fired::None, t.update(false, false, true, 0, kThreshold));
  EXPECT_EQ(Fired::None, t.update(true, true, false, 0, kThreshold));
  EXPECT_EQ(Fired::Short, t.update(false, false, true, 0, kThreshold));
}

// A button with nothing bound to a hold has no ambiguity to resolve, so it acts the
// instant it goes down. That is the whole point of the sweep: no waiting for a
// release the firmware never needed.
TEST(HoldButton, WithoutAHoldActionTheShortOneFiresOnThePress) {
  Tracker t;
  EXPECT_EQ(Fired::Short, t.updatePressOnly(/*wasPressedNow=*/true));
  EXPECT_EQ(Fired::None, t.updatePressOnly(/*wasPressedNow=*/false));
}

// The hint band on a touch device stands in for the front buttons, and it has to
// deliver a whole stroke: the press on the frame the tap lands and the release on
// the next. Both halves in one frame is what a physical key never does, and it
// left this tracker with a release it was not armed for, so a tapped "Open" in
// the file browser did nothing at all.
TEST(HoldButton, ATapDeliveredAsPressThenReleaseIsAShortPress) {
  hold_button::Tracker tracker;
  EXPECT_EQ(tracker.update(true, false, false, 0, 700), hold_button::Fired::None);
  EXPECT_EQ(tracker.update(false, false, true, 0, 700), hold_button::Fired::Short);
}

TEST(HoldButton, ATapThatOnlyEverPressesFiresNothing) {
  // The shape the bug had: the synthesized tap answered whichever of the two
  // queries the caller wrote first and was spent, so the tracker was armed by a
  // press whose release never arrived and the button read as dead.
  hold_button::Tracker tracker;
  EXPECT_EQ(tracker.update(true, false, false, 0, 700), hold_button::Fired::None);
  EXPECT_EQ(tracker.update(false, false, false, 0, 700), hold_button::Fired::None);
  EXPECT_EQ(tracker.update(false, false, false, 0, 700), hold_button::Fired::None);
}
