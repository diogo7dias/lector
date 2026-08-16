// Host proof of the gate that stops one physical press acting on two screens.
//
// Buttons act on the press now, so the press that opens a screen is usually still
// held when that screen appears. The gate makes the new screen wait for the release
// before it reads anything.

#include <gtest/gtest.h>

#include "util/InputArmingPolicy.h"

using input_arming::Gate;

// Nothing has opened anything yet: the first screen after boot reads input at once.
TEST(InputArming, StartsLive) {
  Gate gate;
  EXPECT_FALSE(gate.armed());
  EXPECT_TRUE(gate.update(/*anythingHeld=*/false));
  EXPECT_TRUE(gate.update(/*anythingHeld=*/true));
}

// The exact case this exists for: the press that opened the screen is still down, so
// the new screen stays deaf until it comes back up.
TEST(InputArming, HeldOpeningPressIsSwallowedUntilRelease) {
  Gate gate;
  gate.arm();
  EXPECT_FALSE(gate.update(/*anythingHeld=*/true));
  EXPECT_FALSE(gate.update(/*anythingHeld=*/true));
  EXPECT_TRUE(gate.update(/*anythingHeld=*/false));
}

// Once released the gate is done, and the next press acts immediately — the instant
// feel is only ever delayed for the press that was already down.
TEST(InputArming, LaterPressesAreNotDelayed) {
  Gate gate;
  gate.arm();
  EXPECT_FALSE(gate.update(/*anythingHeld=*/true));
  EXPECT_TRUE(gate.update(/*anythingHeld=*/false));
  EXPECT_TRUE(gate.update(/*anythingHeld=*/true));
  EXPECT_FALSE(gate.armed());
}

// A screen opened by something other than a button (a timeout, a finished sync) has
// nothing held, so it is live on its very first pass and loses no press.
TEST(InputArming, ScreenOpenedWithNothingHeldIsLiveImmediately) {
  Gate gate;
  gate.arm();
  EXPECT_TRUE(gate.update(/*anythingHeld=*/false));
}

// Arming again while already waiting does not reset anything: a screen that opens
// another screen while the button is still down still needs exactly one release.
TEST(InputArming, ReArmingWhileWaitingIsIdempotent) {
  Gate gate;
  gate.arm();
  EXPECT_FALSE(gate.update(/*anythingHeld=*/true));
  gate.arm();
  EXPECT_FALSE(gate.update(/*anythingHeld=*/true));
  EXPECT_TRUE(gate.update(/*anythingHeld=*/false));
}
