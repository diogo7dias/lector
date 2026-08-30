#include <gtest/gtest.h>

#include "components/SignalMeter.h"

namespace {

constexpr int kBandRight = 480;
constexpr int kBandBottom = 100;

signal_meter::Rect icon() { return signal_meter::iconRect(kBandRight, kBandBottom); }

}  // namespace

TEST(SignalMeter, TheIconHugsTheRightEdgeOfItsBand) {
  const auto rect = icon();
  EXPECT_EQ(rect.x + rect.width, kBandRight);
  EXPECT_EQ(rect.y + rect.height, kBandBottom);
  EXPECT_EQ(rect.height, signal_meter::kHeight);
}

TEST(SignalMeter, TheBarsClimbAndStandOnOneBaseline) {
  const auto rect = icon();
  int previous = 0;
  for (int i = 0; i < signal_meter::kBarCount; ++i) {
    const auto bar = signal_meter::barRect(rect, i);
    EXPECT_GT(bar.height, previous) << "bar " << i << " is not taller than the one before it";
    previous = bar.height;
    EXPECT_EQ(bar.y + bar.height, rect.y + rect.height) << "bar " << i << " left the baseline";
  }
}

TEST(SignalMeter, TheBarsStayInsideTheIconAndDoNotTouch) {
  const auto rect = icon();
  for (int i = 0; i < signal_meter::kBarCount; ++i) {
    const auto bar = signal_meter::barRect(rect, i);
    EXPECT_GE(bar.x, rect.x);
    EXPECT_LE(bar.x + bar.width, rect.x + rect.width);
    if (i > 0) {
      const auto previous = signal_meter::barRect(rect, i - 1);
      EXPECT_EQ(bar.x - (previous.x + previous.width), signal_meter::kBarGap);
    }
  }
}

TEST(SignalMeter, TheTallestBarIsTheWholeIcon) {
  const auto rect = icon();
  EXPECT_EQ(signal_meter::barRect(rect, signal_meter::kBarCount - 1).height, signal_meter::kHeight);
}

TEST(SignalMeter, OnlyTheLitBarsAreFilled) {
  EXPECT_TRUE(signal_meter::barIsFilled(0, 2));
  EXPECT_TRUE(signal_meter::barIsFilled(1, 2));
  EXPECT_FALSE(signal_meter::barIsFilled(2, 2));
  EXPECT_FALSE(signal_meter::barIsFilled(0, 0));
}
