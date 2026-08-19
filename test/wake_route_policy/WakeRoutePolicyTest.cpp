// Where a wake lands, for the two sleep faces that override the ordinary routing.
//
// Quick Resume must be invisible: the screen you locked from is the screen you get
// back, and nothing else may divert it — not "Open a random book on boot", not a
// held Back. The Light face is the opposite promise: it names a book on the sleep
// screen, so the wake must open that book even when you locked from the home menu.
#include <gtest/gtest.h>

#include "WakeRoutePolicy.h"

using wake_route::Route;
using wake_route::WakeInputs;

namespace {

// A quick-resume wake that locked inside a book, with nothing wrong.
WakeInputs quickResumeFromBook() {
  WakeInputs in;
  in.quickResume = true;
  in.quickResumeTargetIsReader = true;
  in.hasBook = true;
  return in;
}

WakeInputs lightForcingBook() {
  WakeInputs in;
  in.forceBookOnWake = true;
  in.hasBook = true;
  return in;
}

}  // namespace

// ── Quick Resume ────────────────────────────────────────────────────────────

TEST(WakeRoutePolicy, QuickResumeFromABookReopensThatBook) {
  EXPECT_EQ(wake_route::resolve(quickResumeFromBook()), Route::ForceReader);
}

TEST(WakeRoutePolicy, QuickResumeFromABookIgnoresRandomBookOnBoot) {
  WakeInputs in = quickResumeFromBook();
  in.randomBookOnBoot = true;
  EXPECT_EQ(wake_route::resolve(in), Route::ForceReader);
}

TEST(WakeRoutePolicy, QuickResumeFromABookIgnoresHeldBack) {
  WakeInputs in = quickResumeFromBook();
  in.backHeld = true;
  EXPECT_EQ(wake_route::resolve(in), Route::ForceReader);
}

// The lock repainted home before stamping the moon, so the frame on the glass IS home.
TEST(WakeRoutePolicy, QuickResumeFromANonBookScreenLandsOnHome) {
  WakeInputs in = quickResumeFromBook();
  in.quickResumeTargetIsReader = false;
  EXPECT_EQ(wake_route::resolve(in), Route::ForceHome);
}

TEST(WakeRoutePolicy, QuickResumeFromANonBookScreenIgnoresRandomBookOnBoot) {
  WakeInputs in = quickResumeFromBook();
  in.quickResumeTargetIsReader = false;
  in.randomBookOnBoot = true;
  EXPECT_EQ(wake_route::resolve(in), Route::ForceHome);
}

// ── Light face forcing the book open ────────────────────────────────────────

TEST(WakeRoutePolicy, LightFaceOpensTheBookEvenWhenLockedFromHome) {
  WakeInputs in = lightForcingBook();
  in.sleptFromReader = false;
  EXPECT_EQ(wake_route::resolve(in), Route::ForceReader);
}

TEST(WakeRoutePolicy, LightFaceOpensTheBookWithBackHeld) {
  WakeInputs in = lightForcingBook();
  in.backHeld = true;
  EXPECT_EQ(wake_route::resolve(in), Route::ForceReader);
}

// ── Safety valves, shared by both ───────────────────────────────────────────

TEST(WakeRoutePolicy, AReaderCrashLastBootAlwaysLandsOnHome) {
  WakeInputs crashed = quickResumeFromBook();
  crashed.readerCrashed = true;
  EXPECT_EQ(wake_route::resolve(crashed), Route::ForceHome);

  WakeInputs light = lightForcingBook();
  light.readerCrashed = true;
  EXPECT_EQ(wake_route::resolve(light), Route::ForceHome);
}

TEST(WakeRoutePolicy, NoBookToOpenLandsOnHome) {
  WakeInputs quick = quickResumeFromBook();
  quick.hasBook = false;
  EXPECT_EQ(wake_route::resolve(quick), Route::ForceHome);

  WakeInputs light = lightForcingBook();
  light.hasBook = false;
  EXPECT_EQ(wake_route::resolve(light), Route::ForceHome);
}

// Every other sleep face keeps the routing it has today, escape hatches included.
TEST(WakeRoutePolicy, OrdinarySleepFacesAreLeftAlone) {
  WakeInputs in;
  in.hasBook = true;
  in.sleptFromReader = true;
  EXPECT_EQ(wake_route::resolve(in), Route::Unchanged);

  in.randomBookOnBoot = true;
  EXPECT_EQ(wake_route::resolve(in), Route::Unchanged);
}

// ── Lock side: what the frame must show before the moon is stamped ──────────

TEST(WakeRoutePolicy, LockingInsideABookNeedsNoRepaint) {
  EXPECT_FALSE(wake_route::quickResumeNeedsHomeRepaint(/*targetIsReader=*/true, /*alreadyOnHome=*/false));
}

TEST(WakeRoutePolicy, LockingOnASettingsScreenRepaintsHomeFirst) {
  EXPECT_TRUE(wake_route::quickResumeNeedsHomeRepaint(/*targetIsReader=*/false, /*alreadyOnHome=*/false));
}

TEST(WakeRoutePolicy, LockingOnHomeItselfNeedsNoRepaint) {
  EXPECT_FALSE(wake_route::quickResumeNeedsHomeRepaint(/*targetIsReader=*/false, /*alreadyOnHome=*/true));
}
