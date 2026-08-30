#include "util/ReleaseGate.h"

#include <gtest/gtest.h>

using input_gate::ReleaseGate;

TEST(ReleaseGateTest, StartsOpen) {
  ReleaseGate gate;
  EXPECT_FALSE(gate.swallowsRelease());
}

TEST(ReleaseGateTest, ArmingWithNothingHeldDoesNothing) {
  ReleaseGate gate;
  gate.arm(false);
  EXPECT_FALSE(gate.swallowsRelease());
}

TEST(ReleaseGateTest, SwallowsTheReleaseOfTheHeldPress) {
  ReleaseGate gate;
  gate.arm(/*anyHeld=*/true);
  // The pass that carries the release edge is the one that must be swallowed.
  gate.tick(/*anyHeld=*/false, /*anyReleased=*/true);
  EXPECT_TRUE(gate.swallowsRelease());
}

TEST(ReleaseGateTest, OpensOnTheFirstQuietPass) {
  ReleaseGate gate;
  gate.arm(true);
  gate.tick(false, true);
  gate.tick(false, false);
  EXPECT_FALSE(gate.swallowsRelease());
}

TEST(ReleaseGateTest, HoldsWhileTheButtonIsStillDown) {
  ReleaseGate gate;
  gate.arm(true);
  gate.tick(/*anyHeld=*/true, /*anyReleased=*/false);
  gate.tick(true, false);
  EXPECT_TRUE(gate.swallowsRelease());
  gate.tick(false, true);
  EXPECT_TRUE(gate.swallowsRelease());
  gate.tick(false, false);
  EXPECT_FALSE(gate.swallowsRelease());
}

TEST(ReleaseGateTest, ANewPressAfterTheGateOpensIsNotSwallowed) {
  ReleaseGate gate;
  gate.arm(true);
  gate.tick(false, true);
  gate.tick(false, false);
  gate.tick(true, false);  // a fresh press, nobody armed the gate for it
  EXPECT_FALSE(gate.swallowsRelease());
}
