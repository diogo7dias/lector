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

// The band keeps its button-era height on touch hardware: a quarter-width slot is an
// easy target already, and a taller band ate a third of the rows above it.
TEST(TouchMetrics, ButtonHintBandKeepsItsHeight) {
  EXPECT_EQ(base().buttonHintsHeight, 40);
  EXPECT_EQ(touch_metrics::adjusted(base()).buttonHintsHeight, base().buttonHintsHeight);
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
  roomy.keyboardKeyHeight = 90;
  const ThemeMetrics touch = touch_metrics::adjusted(roomy);
  EXPECT_EQ(touch.listRowHeight, 80);
  EXPECT_EQ(touch.keyboardKeyHeight, 90);
}

// The one band that is narrow as well as short clears the floor. Rows and hint slots
// are exempt by design: the width they span is what makes them hittable.
TEST(TouchMetrics, TheNarrowTouchTargetClearsTheFortyEightPixelFloor) {
  EXPECT_GE(touch_metrics::adjusted(base()).keyboardKeyHeight, touch_metrics::kMinTarget);
}

}  // namespace
