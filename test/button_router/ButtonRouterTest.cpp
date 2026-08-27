// Host tests for the per-button binding router.
//
// The router is the part that turns "this key went down" into "run this action": it arms
// each key's gesture detector from what the user actually bound in the current context,
// and names the action the gesture landed on. The gesture timing itself belongs to
// button_gestures and is tested there; what matters here is the arming and the naming.
#include <gtest/gtest.h>

#include "util/ButtonRouter.h"

namespace {

using bound_action::LP_MENU_BOOKMARK;
using bound_action::LP_MENU_DISABLED;
using bound_action::LP_MENU_LIGHT_PANEL;
using bound_action::LP_MENU_PAGE_NEXT;
using bound_action::LP_MENU_SLEEP;
using button_router::Router;

constexpr int LEFT = 0;
constexpr int HOME = 2;
constexpr int POWER = 3;

// A press and its release, far enough apart to be a click and not a hold.
button_router::Fired click(Router& router, const int key, uint32_t& now) {
  router.onPress(key, now);
  now += 50;
  return router.onRelease(key, now);
}

}  // namespace

TEST(ButtonRouter, ASingleClickNamesTheActionBoundToIt) {
  Router router;
  router.configure(LEFT, {LP_MENU_PAGE_NEXT, LP_MENU_DISABLED, LP_MENU_DISABLED});
  uint32_t now = 1000;
  const auto fired = click(router, LEFT, now);
  EXPECT_TRUE(fired.valid);
  EXPECT_EQ(fired.function, LP_MENU_PAGE_NEXT);
}

TEST(ButtonRouter, WithNothingOnTheDoubleTheSingleFiresOnRelease) {
  // The whole point of arming per binding: a key that carries no double must not make
  // the page turn wait for a second press that is never coming.
  Router router;
  router.configure(LEFT, {LP_MENU_PAGE_NEXT, LP_MENU_DISABLED, LP_MENU_DISABLED});
  uint32_t now = 1000;
  EXPECT_TRUE(click(router, LEFT, now).valid);
  EXPECT_FALSE(router.busy(LEFT));
}

TEST(ButtonRouter, WithADoubleBoundTheSingleWaitsForTheWindowToClose) {
  Router router;
  router.configure(LEFT, {LP_MENU_PAGE_NEXT, LP_MENU_LIGHT_PANEL, LP_MENU_DISABLED});
  uint32_t now = 1000;
  EXPECT_FALSE(click(router, LEFT, now).valid);
  EXPECT_TRUE(router.busy(LEFT));

  now += button_gestures::DOUBLE_WINDOW_MS;
  const auto fired = router.tick(LEFT, now);
  EXPECT_TRUE(fired.valid);
  EXPECT_EQ(fired.function, LP_MENU_PAGE_NEXT);
  EXPECT_FALSE(router.busy(LEFT));
}

TEST(ButtonRouter, ASecondPressInsideTheWindowNamesTheDoubleAction) {
  Router router;
  router.configure(LEFT, {LP_MENU_PAGE_NEXT, LP_MENU_LIGHT_PANEL, LP_MENU_DISABLED});
  uint32_t now = 1000;
  click(router, LEFT, now);
  now += 100;
  const auto fired = router.onPress(LEFT, now);
  EXPECT_TRUE(fired.valid);
  EXPECT_EQ(fired.function, LP_MENU_LIGHT_PANEL);
}

TEST(ButtonRouter, AHoldFiresWhileTheKeyIsStillDown) {
  Router router;
  router.configure(LEFT, {LP_MENU_PAGE_NEXT, LP_MENU_DISABLED, LP_MENU_SLEEP});
  uint32_t now = 1000;
  router.onPress(LEFT, now);
  now += button_gestures::HOLD_MS;
  const auto fired = router.tick(LEFT, now);
  EXPECT_TRUE(fired.valid);
  EXPECT_EQ(fired.function, LP_MENU_SLEEP);
}

TEST(ButtonRouter, AKeyThatReportsItsOwnLongPressNamesTheHoldDirectly) {
  // The capacitive Home key reports a completed long press rather than a held edge, so
  // its hold cannot be timed here.
  Router router;
  router.configure(LEFT, {LP_MENU_PAGE_NEXT, LP_MENU_DISABLED, LP_MENU_SLEEP});
  const auto fired = router.fireHold(LEFT);
  EXPECT_TRUE(fired.valid);
  EXPECT_EQ(fired.function, LP_MENU_SLEEP);
}

TEST(ButtonRouter, FiringTheHoldDropsAGestureStillInFlight) {
  // The key may report a tap alongside its long press; that tap must not fire as well.
  Router router;
  router.configure(LEFT, {LP_MENU_PAGE_NEXT, LP_MENU_LIGHT_PANEL, LP_MENU_SLEEP});
  uint32_t now = 1000;
  click(router, LEFT, now);
  ASSERT_TRUE(router.busy(LEFT));
  EXPECT_TRUE(router.fireHold(LEFT).valid);
  EXPECT_FALSE(router.busy(LEFT));
  now += button_gestures::DOUBLE_WINDOW_MS;
  EXPECT_FALSE(router.tick(LEFT, now).valid);
}

TEST(ButtonRouter, FiringTheHoldOnAKeyWithNoHoldBoundDoesNothing) {
  Router router;
  router.configure(LEFT, {LP_MENU_PAGE_NEXT, LP_MENU_DISABLED, LP_MENU_DISABLED});
  EXPECT_FALSE(router.fireHold(LEFT).valid);
}

TEST(ButtonRouter, AnUnboundGestureNeverFires) {
  Router router;
  router.configure(LEFT, {LP_MENU_DISABLED, LP_MENU_DISABLED, LP_MENU_DISABLED});
  uint32_t now = 1000;
  EXPECT_FALSE(click(router, LEFT, now).valid);
  now += 2000;
  EXPECT_FALSE(router.tick(LEFT, now).valid);
}

TEST(ButtonRouter, PagingActionsAskForTheRawEdgeInsteadOfBeingDispatched) {
  // Prev/Next page on a side key IS what the key already does; replaying the edge lets
  // the reader and every list keep their own paging code, including its repeat.
  Router router;
  router.configure(LEFT, {LP_MENU_PAGE_NEXT, LP_MENU_DISABLED, LP_MENU_DISABLED});
  uint32_t now = 1000;
  const auto fired = click(router, LEFT, now);
  EXPECT_TRUE(fired.valid);
  EXPECT_TRUE(fired.replayRawEdge);
}

TEST(ButtonRouter, EveryOtherActionIsDispatchedRatherThanReplayed) {
  Router router;
  router.configure(LEFT, {LP_MENU_BOOKMARK, LP_MENU_DISABLED, LP_MENU_DISABLED});
  uint32_t now = 1000;
  const auto fired = click(router, LEFT, now);
  EXPECT_TRUE(fired.valid);
  EXPECT_FALSE(fired.replayRawEdge);
}

TEST(ButtonRouter, AKeyWithOnlyItsDefaultSingleNeedsNoGating) {
  // Nothing bound beyond the paging the key already does: the router must not suppress
  // a single edge, or holding the key to page-repeat through a list would stop working.
  Router router;
  router.configure(LEFT, {LP_MENU_PAGE_NEXT, LP_MENU_DISABLED, LP_MENU_DISABLED});
  EXPECT_FALSE(router.intercepts(LEFT));
}

TEST(ButtonRouter, AKeyWithADoubleOrAHoldIsIntercepted) {
  Router router;
  router.configure(LEFT, {LP_MENU_PAGE_NEXT, LP_MENU_LIGHT_PANEL, LP_MENU_DISABLED});
  EXPECT_TRUE(router.intercepts(LEFT));
  router.configure(LEFT, {LP_MENU_PAGE_NEXT, LP_MENU_DISABLED, LP_MENU_SLEEP});
  EXPECT_TRUE(router.intercepts(LEFT));
}

TEST(ButtonRouter, AKeyWhoseSingleWasRemappedIsInterceptedToo) {
  // The single no longer means what the key natively does, so the raw edge must be
  // hidden even though no double or hold is bound.
  Router router;
  router.configure(LEFT, {LP_MENU_BOOKMARK, LP_MENU_DISABLED, LP_MENU_DISABLED});
  EXPECT_TRUE(router.intercepts(LEFT));
}

TEST(ButtonRouter, OnlyAHoldBindingCostsTheKeyItsRepeat) {
  Router router;
  router.configure(LEFT, {LP_MENU_PAGE_NEXT, LP_MENU_LIGHT_PANEL, LP_MENU_DISABLED});
  EXPECT_FALSE(router.suppressesHold(LEFT));
  router.configure(LEFT, {LP_MENU_PAGE_NEXT, LP_MENU_DISABLED, LP_MENU_SLEEP});
  EXPECT_TRUE(router.suppressesHold(LEFT));
}

TEST(ButtonRouter, ReconfiguringClearsAPendingGesture) {
  // Leaving a book re-arms every key. A click still waiting for its window must not
  // fire into the screen that replaced the one it was pressed on.
  Router router;
  router.configure(LEFT, {LP_MENU_PAGE_NEXT, LP_MENU_LIGHT_PANEL, LP_MENU_DISABLED});
  uint32_t now = 1000;
  click(router, LEFT, now);
  ASSERT_TRUE(router.busy(LEFT));
  router.configure(LEFT, {LP_MENU_PAGE_NEXT, LP_MENU_DISABLED, LP_MENU_DISABLED});
  EXPECT_FALSE(router.busy(LEFT));
  now += button_gestures::DOUBLE_WINDOW_MS;
  EXPECT_FALSE(router.tick(LEFT, now).valid);
}

TEST(ButtonRouter, TheHomeKeyTreatsHomeAsWhatItAlreadyDoes) {
  // Home on the Home key is not a remap, so the key must not be intercepted at all: no
  // gating, no detector, no delay for anyone who leaves the bindings alone.
  Router router;
  router.configure(HOME, {bound_action::LP_MENU_GO_HOME, LP_MENU_DISABLED, LP_MENU_DISABLED},
                   button_router::NATIVE_HOME_KEY);
  EXPECT_FALSE(router.intercepts(HOME));
}

TEST(ButtonRouter, PagingBoundToTheHomeKeyIsDispatchedRatherThanReplayed) {
  // The Home key does not page, so replaying its tap would go home instead.
  Router router;
  router.configure(HOME, {LP_MENU_PAGE_NEXT, LP_MENU_DISABLED, LP_MENU_DISABLED}, button_router::NATIVE_HOME_KEY);
  ASSERT_TRUE(router.intercepts(HOME));
  uint32_t now = 1000;
  const auto fired = click(router, HOME, now);
  EXPECT_TRUE(fired.valid);
  EXPECT_FALSE(fired.replayRawEdge);
}

TEST(ButtonRouter, HomeBoundToASideKeyIsDispatchedRatherThanReplayed) {
  Router router;
  router.configure(LEFT, {bound_action::LP_MENU_GO_HOME, LP_MENU_DISABLED, LP_MENU_DISABLED});
  const auto fired = [&] {
    uint32_t now = 1000;
    return click(router, LEFT, now);
  }();
  EXPECT_TRUE(fired.valid);
  EXPECT_FALSE(fired.replayRawEdge);
}

TEST(ButtonRouter, ThePowerKeyIsTheFourthKey) { EXPECT_EQ(button_router::KEY_COUNT, 4); }

TEST(ButtonRouter, SleepOnThePowerHoldIsWhatTheKeyAlreadyDoes) {
  // Power hold sleeps today, at the user's own sleepHoldMs. Leaving that binding alone
  // must not put the key behind the router, or the threshold would silently become 500 ms.
  Router router;
  router.configure(POWER, {LP_MENU_DISABLED, LP_MENU_DISABLED, LP_MENU_SLEEP}, button_router::NATIVE_POWER_KEY);
  EXPECT_FALSE(router.intercepts(POWER));
}

TEST(ButtonRouter, SleepOnThePowerSingleIsDispatchedRatherThanReplayed) {
  // Native is per gesture: a power RELEASE has never slept the device, so replaying it
  // would do nothing at all.
  Router router;
  router.configure(POWER, {LP_MENU_SLEEP, LP_MENU_DISABLED, LP_MENU_DISABLED}, button_router::NATIVE_POWER_KEY);
  ASSERT_TRUE(router.intercepts(POWER));
  uint32_t now = 1000;
  const auto fired = click(router, POWER, now);
  EXPECT_TRUE(fired.valid);
  EXPECT_FALSE(fired.replayRawEdge);
}

TEST(ButtonRouter, PagingOnASideKeyHoldIsStillReplayed) {
  // The side keys repeat while held, so a hold bound back to paging keeps the firmware's
  // own repeat rather than firing one page at half a second.
  Router router;
  router.configure(LEFT, {LP_MENU_DISABLED, LP_MENU_DISABLED, LP_MENU_PAGE_NEXT});
  uint32_t now = 1000;
  router.onPress(LEFT, now);
  now += button_gestures::HOLD_MS;
  const auto fired = router.tick(LEFT, now);
  EXPECT_TRUE(fired.valid);
  EXPECT_TRUE(fired.replayRawEdge);
}

TEST(ButtonRouter, AKeyCanBeGivenItsOwnHoldThreshold) {
  // Power keeps sleepHoldMs; every other key holds at the shared 500 ms.
  Router router;
  router.configure(POWER, {LP_MENU_DISABLED, LP_MENU_DISABLED, LP_MENU_BOOKMARK}, button_router::NATIVE_POWER_KEY);
  router.setHoldMs(POWER, 2000);
  uint32_t now = 1000;
  router.onPress(POWER, now);
  now += 1500;
  EXPECT_FALSE(router.tick(POWER, now).valid);
  now += 600;
  EXPECT_TRUE(router.tick(POWER, now).valid);
}

// Reported from a device: with this exact set the single click and the double click both
// worked and the hold never fired. It does fire here, which is what moved the search off
// the router and onto what the action itself does.
TEST(ButtonRouter, TheHoldFiresWithAPagePrevSingleAndABoundDouble) {
  button_router::Router router;
  router.configure(0,
                   button_router::Binding{bound_action::LP_MENU_PAGE_PREV, bound_action::LP_MENU_FORCE_REFRESH,
                                          bound_action::LP_MENU_LIGHT_PANEL},
                   button_router::NATIVE_SIDE_KEY);
  EXPECT_TRUE(router.intercepts(0));
  EXPECT_TRUE(router.suppressesHold(0));

  EXPECT_FALSE(router.onPress(0, 1000).valid);
  EXPECT_FALSE(router.tick(0, 1400).valid);
  const auto held = router.tick(0, 1500);
  ASSERT_TRUE(held.valid);
  EXPECT_EQ(held.function, bound_action::LP_MENU_LIGHT_PANEL);
  EXPECT_FALSE(held.replayRawEdge);
}
