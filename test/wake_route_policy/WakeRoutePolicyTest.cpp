#include <gtest/gtest.h>

#include "WakeRoutePolicy.h"

using wake_route::Route;
using wake_route::WakeInputs;

namespace {

WakeInputs lightForcingBook() {
  WakeInputs in;
  in.forceBookOnWake = true;
  in.hasBook = true;
  return in;
}

}  // namespace

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
  WakeInputs light = lightForcingBook();
  light.readerCrashed = true;
  EXPECT_EQ(wake_route::resolve(light), Route::ForceHome);
}

TEST(WakeRoutePolicy, NoBookToOpenLandsOnHome) {
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

  in.bookOnBoot = true;
  EXPECT_EQ(wake_route::resolve(in), Route::Unchanged);
}
