#include <LowBatteryPolicy.h>
#include <gtest/gtest.h>

namespace {

using low_battery::Action;
using low_battery::Inputs;

Inputs lowAndReadable() {
  Inputs in;
  in.percent = low_battery::WARN_PERCENT;
  in.screenAllowed = true;
  return in;
}

TEST(LowBatteryPolicy, WarnsOnceAtTheThreshold) {
  EXPECT_EQ(low_battery::resolve(lowAndReadable()), Action::Warn);

  Inputs below = lowAndReadable();
  below.percent = 1;
  EXPECT_EQ(low_battery::resolve(below), Action::Warn);
}

TEST(LowBatteryPolicy, StaysQuietAboveTheThreshold) {
  Inputs in = lowAndReadable();
  in.percent = low_battery::WARN_PERCENT + 1;
  EXPECT_EQ(low_battery::resolve(in), Action::None);
}

TEST(LowBatteryPolicy, TreatsAZeroReadingAsNoReading) {
  // A failed gauge read and a board with no battery backend both report zero; warning on
  // it would fire a "Battery low (0%)" that means nothing.
  Inputs in = lowAndReadable();
  in.percent = 0;
  EXPECT_EQ(low_battery::resolve(in), Action::None);

  in.alreadyWarned = true;
  EXPECT_EQ(low_battery::resolve(in), Action::None);
}

TEST(LowBatteryPolicy, DoesNotWarnWhilePluggedIn) {
  Inputs in = lowAndReadable();
  in.usbConnected = true;
  EXPECT_EQ(low_battery::resolve(in), Action::None);
}

TEST(LowBatteryPolicy, DoesNotWarnOverAScreenThatMustNotBeInterrupted) {
  Inputs in = lowAndReadable();
  in.screenAllowed = false;
  EXPECT_EQ(low_battery::resolve(in), Action::None);
}

TEST(LowBatteryPolicy, WarnsOnlyOncePerDrain) {
  Inputs in = lowAndReadable();
  in.alreadyWarned = true;
  EXPECT_EQ(low_battery::resolve(in), Action::None);

  // Still latched between the warn and clear levels: the hysteresis gap.
  in.percent = low_battery::CLEAR_PERCENT;
  EXPECT_EQ(low_battery::resolve(in), Action::None);
}

TEST(LowBatteryPolicy, ReArmsOnceTheChargeIsBackUp) {
  Inputs in = lowAndReadable();
  in.alreadyWarned = true;
  in.percent = low_battery::CLEAR_PERCENT + 1;
  EXPECT_EQ(low_battery::resolve(in), Action::ClearLatch);
}

TEST(LowBatteryPolicy, ReArmsEvenWhileChargingOnAScreenItCouldNotHaveWarnedOver) {
  // Clearing the latch shows nothing, so neither guard applies to it.
  Inputs in;
  in.percent = 90;
  in.alreadyWarned = true;
  in.usbConnected = true;
  in.screenAllowed = false;
  EXPECT_EQ(low_battery::resolve(in), Action::ClearLatch);
}

}  // namespace
