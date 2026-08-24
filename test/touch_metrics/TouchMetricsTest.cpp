#include <gtest/gtest.h>

#include "components/themes/TouchMetrics.h"

namespace {

ThemeMetrics base() { return BaseMetrics::values; }

// Rows keep the button-era height on a touch board too: they span the full width, so
// they are already a wide target, and matching the home screen's rows matters more than
// the extra millimetre.
TEST(TouchMetrics, ListRowsKeepTheirHeight) {
  EXPECT_EQ(base().listRowHeight, 30);
  EXPECT_EQ(touch_metrics::adjusted(base()).listRowHeight, base().listRowHeight);
  EXPECT_EQ(touch_metrics::adjusted(base()).listWithSubtitleRowHeight, base().listWithSubtitleRowHeight);
  EXPECT_EQ(touch_metrics::adjusted(base()).menuRowHeight, base().menuRowHeight);
}

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

// The bands that are narrow as well as short clear the floor. Rows are exempt by
// design: full-screen width is what makes them hittable.
TEST(TouchMetrics, TheNarrowTouchTargetsClearTheFortyEightPixelFloor) {
  const ThemeMetrics touch = touch_metrics::adjusted(base());
  EXPECT_GE(touch.buttonHintsHeight, touch_metrics::kMinTarget);
  EXPECT_GE(touch.keyboardKeyHeight, touch_metrics::kMinTarget);
}

}  // namespace
