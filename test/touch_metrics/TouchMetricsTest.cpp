#include <gtest/gtest.h>

#include "components/themes/TouchMetrics.h"

namespace {

ThemeMetrics base() { return BaseMetrics::values; }

TEST(TouchMetrics, ListRowsReachTheFingerTarget) {
  EXPECT_EQ(base().listRowHeight, 30);
  EXPECT_EQ(touch_metrics::adjusted(base()).listRowHeight, 56);
}

TEST(TouchMetrics, SubtitleRowsGrowWithTheirTwoLines) {
  EXPECT_EQ(touch_metrics::adjusted(base()).listWithSubtitleRowHeight, 72);
}

TEST(TouchMetrics, MenuRowsReachTheFingerTarget) { EXPECT_EQ(touch_metrics::adjusted(base()).menuRowHeight, 60); }

TEST(TouchMetrics, ButtonHintBandBecomesATapTarget) {
  EXPECT_EQ(touch_metrics::adjusted(base()).buttonHintsHeight, 56);
}

TEST(TouchMetrics, KeyboardKeysReachTheFingerTarget) {
  EXPECT_EQ(touch_metrics::adjusted(base()).keyboardKeyHeight, 62);
}

TEST(TouchMetrics, NonInteractiveMetricsAreLeftAlone) {
  const ThemeMetrics touch = touch_metrics::adjusted(base());
  EXPECT_EQ(touch.headerHeight, base().headerHeight);
  EXPECT_EQ(touch.contentSidePadding, base().contentSidePadding);
  EXPECT_EQ(touch.progressBarHeight, base().progressBarHeight);
}

TEST(TouchMetrics, AlreadyRoomyMetricsAreNotShrunk) {
  ThemeMetrics roomy = base();
  roomy.listRowHeight = 80;
  roomy.buttonHintsHeight = 90;
  const ThemeMetrics touch = touch_metrics::adjusted(roomy);
  EXPECT_EQ(touch.listRowHeight, 80);
  EXPECT_EQ(touch.buttonHintsHeight, 90);
}

TEST(TouchMetrics, EveryTouchTargetClearsTheFortyEightPixelFloor) {
  const ThemeMetrics touch = touch_metrics::adjusted(base());
  EXPECT_GE(touch.listRowHeight, touch_metrics::kMinTarget);
  EXPECT_GE(touch.listWithSubtitleRowHeight, touch_metrics::kMinTarget);
  EXPECT_GE(touch.menuRowHeight, touch_metrics::kMinTarget);
  EXPECT_GE(touch.buttonHintsHeight, touch_metrics::kMinTarget);
  EXPECT_GE(touch.keyboardKeyHeight, touch_metrics::kMinTarget);
}

}  // namespace
