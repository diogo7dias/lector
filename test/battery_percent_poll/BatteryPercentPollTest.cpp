#include <gtest/gtest.h>

#include <functional>

#include "BatteryPercentPoll.h"

namespace {
struct Reader {
  uint16_t value = 0;
  bool ok = true;
  int reads = 0;
  bool operator()(uint16_t& percent) {
    reads++;
    percent = value;
    return ok;
  }
};
}  // namespace

TEST(BatteryPercentPoll, RepeatedDrawsInOnePollDoNotReadOrSmoothAgain) {
  BatteryPercentPoll poll;
  Reader adc{80};
  EXPECT_EQ(poll.get(false, 1000, std::ref(adc)), 80);
  adc.value = 40;
  for (int i = 0; i < 20; ++i) EXPECT_EQ(poll.get(false, 1000 + i, std::ref(adc)), 80);
  EXPECT_EQ(adc.reads, 1);
}

TEST(BatteryPercentPoll, AdcReadingMovesTheValueByOneTenthPerPoll) {
  BatteryPercentPoll poll;
  Reader adc{80};
  poll.get(false, 1000, std::ref(adc));
  adc.value = 40;
  EXPECT_EQ(poll.get(false, 1000 + BatteryPercentPoll::POLL_MS, std::ref(adc)), 76);
}

TEST(BatteryPercentPoll, TheFirstReadAtTimeZeroCounts) {
  BatteryPercentPoll poll;
  Reader adc{55};
  EXPECT_EQ(poll.get(false, 0, std::ref(adc)), 55);
  EXPECT_EQ(poll.get(false, 1, std::ref(adc)), 55);
  EXPECT_EQ(adc.reads, 1);
}

TEST(BatteryPercentPoll, GaugeValueIsNotSmoothed) {
  BatteryPercentPoll poll;
  Reader gauge{70};
  EXPECT_EQ(poll.get(true, 1000, std::ref(gauge)), 70);
  gauge.value = 30;
  EXPECT_EQ(poll.get(true, 1000 + BatteryPercentPoll::POLL_MS, std::ref(gauge)), 30);
}

TEST(BatteryPercentPoll, AFailedGaugeReadKeepsTheLastValue) {
  BatteryPercentPoll poll;
  Reader gauge{70};
  poll.get(true, 1000, std::ref(gauge));
  gauge.ok = false;
  gauge.value = 5;
  EXPECT_EQ(poll.get(true, 1000 + BatteryPercentPoll::POLL_MS, std::ref(gauge)), 70);
}
