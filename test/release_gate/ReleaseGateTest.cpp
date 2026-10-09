#include <gtest/gtest.h>

#include "util/ReleaseGate.h"

using input_gate::ReleaseGate;

namespace {
constexpr uint8_t CONFIRM = 1;
constexpr uint8_t RIGHT = 3;
constexpr uint8_t bit(const uint8_t b) { return static_cast<uint8_t>(1u << b); }
}  // namespace

TEST(ReleaseGateTest, StartsOpen) {
  ReleaseGate gate;
  EXPECT_FALSE(gate.swallowsRelease(CONFIRM));
  EXPECT_FALSE(gate.armed());
}

TEST(ReleaseGateTest, ArmingWithNothingHeldDoesNothing) {
  ReleaseGate gate;
  gate.arm(0);
  EXPECT_FALSE(gate.swallowsRelease(CONFIRM));
}

TEST(ReleaseGateTest, SwallowsTheReleaseOfTheHeldPress) {
  ReleaseGate gate;
  gate.arm(bit(CONFIRM));
  // The pass that carries the release edge is the one that must be swallowed.
  gate.tick(/*held=*/0, /*released=*/bit(CONFIRM));
  EXPECT_TRUE(gate.swallowsRelease(CONFIRM));
}

TEST(ReleaseGateTest, OpensOnTheFirstQuietPass) {
  ReleaseGate gate;
  gate.arm(bit(CONFIRM));
  gate.tick(0, bit(CONFIRM));
  gate.tick(0, 0);
  EXPECT_FALSE(gate.swallowsRelease(CONFIRM));
  EXPECT_FALSE(gate.armed());
}

TEST(ReleaseGateTest, HoldsWhileTheButtonIsStillDown) {
  ReleaseGate gate;
  gate.arm(bit(CONFIRM));
  gate.tick(bit(CONFIRM), 0);
  gate.tick(bit(CONFIRM), 0);
  EXPECT_TRUE(gate.swallowsRelease(CONFIRM));
  gate.tick(0, bit(CONFIRM));
  EXPECT_TRUE(gate.swallowsRelease(CONFIRM));
  gate.tick(0, 0);
  EXPECT_FALSE(gate.swallowsRelease(CONFIRM));
}

TEST(ReleaseGateTest, ANewPressAfterTheGateOpensIsNotSwallowed) {
  ReleaseGate gate;
  gate.arm(bit(CONFIRM));
  gate.tick(0, bit(CONFIRM));
  gate.tick(0, 0);
  gate.tick(bit(CONFIRM), 0);  // a fresh press, nobody armed the gate for it
  EXPECT_FALSE(gate.swallowsRelease(CONFIRM));
}

TEST(ReleaseGateTest, AnotherKeyTappedWhileTheArmedOneIsHeldStillReleases) {
  ReleaseGate gate;
  gate.arm(bit(CONFIRM));
  gate.tick(bit(CONFIRM) | bit(RIGHT), 0);
  gate.tick(bit(CONFIRM), bit(RIGHT));
  EXPECT_FALSE(gate.swallowsRelease(RIGHT));
  EXPECT_TRUE(gate.swallowsRelease(CONFIRM));
}
