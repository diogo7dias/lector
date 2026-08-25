#include <gtest/gtest.h>

#include "components/LightPanelGeometry.h"

namespace {

constexpr int kScreenWidth = 480;
constexpr int kLineHeight = 25;

light_panel::Layout warm() { return light_panel::forScreen(kScreenWidth, kLineHeight, /*hasWarmth=*/true); }
light_panel::Layout plain() { return light_panel::forScreen(kScreenWidth, kLineHeight, /*hasWarmth=*/false); }

TEST(LightPanelGeometry, ThePanelIsABandAcrossTheTopOfTheScreen) {
  const auto layout = warm();
  EXPECT_EQ(layout.x, 0);
  EXPECT_EQ(layout.y, 0);
  EXPECT_EQ(layout.width, kScreenWidth);
  EXPECT_GT(layout.height, kLineHeight * 3);
  // A band, not a screen: it has to leave the page it is drawn over readable.
  EXPECT_LT(layout.height, 240);
}

TEST(LightPanelGeometry, AWarmthlessBoardLosesTheRowRatherThanKeepingItEmpty) {
  const auto with = warm();
  const auto without = plain();
  EXPECT_TRUE(with.hasWarmth);
  EXPECT_FALSE(without.hasWarmth);
  EXPECT_EQ(without.warmth.height, 0);
  EXPECT_LT(without.height, with.height);
}

TEST(LightPanelGeometry, BothBarsShareOneRightHandColumn) {
  const auto layout = warm();
  EXPECT_GT(layout.brightness.bar.width, 0);
  EXPECT_EQ(layout.brightness.bar.x, layout.warmth.bar.x);
  EXPECT_EQ(layout.brightness.bar.width, layout.warmth.bar.width);
  EXPECT_LE(layout.brightness.bar.x + layout.brightness.bar.width, layout.width);
}

TEST(LightPanelGeometry, RowsStackInsideTheBandWithoutOverlapping) {
  const auto layout = warm();
  EXPECT_GE(layout.toggle.y, layout.y);
  EXPECT_LE(layout.toggle.y + layout.toggle.height, layout.brightness.y);
  EXPECT_LE(layout.brightness.y + layout.brightness.height, layout.warmth.y);
  EXPECT_LE(layout.warmth.y + layout.warmth.height, layout.y + layout.height);
}

TEST(LightPanelGeometry, ATouchLandsOnTheRowItIsOver) {
  const auto layout = warm();
  const int x = layout.brightness.bar.x + 4;
  EXPECT_EQ(light_panel::rowAt(layout, x, layout.toggle.y + layout.toggle.height / 2), light_panel::Row::Toggle);
  EXPECT_EQ(light_panel::rowAt(layout, x, layout.brightness.y + layout.brightness.height / 2),
            light_panel::Row::Brightness);
  EXPECT_EQ(light_panel::rowAt(layout, x, layout.warmth.y + layout.warmth.height / 2), light_panel::Row::Warmth);
}

TEST(LightPanelGeometry, ATouchBelowTheBandBelongsToThePageNotThePanel) {
  const auto layout = warm();
  const int below = layout.y + layout.height + 1;
  EXPECT_FALSE(light_panel::insidePanel(layout, 10, below));
  EXPECT_EQ(light_panel::rowAt(layout, 10, below), light_panel::Row::None);
  EXPECT_TRUE(light_panel::insidePanel(layout, 10, layout.y + 1));
}

TEST(LightPanelGeometry, TheWarmthRowIsUnreachableWhenTheBoardHasNoWarmth) {
  const auto layout = plain();
  const int past = layout.y + layout.height - 1;
  EXPECT_NE(light_panel::rowAt(layout, 10, past), light_panel::Row::Warmth);
}

TEST(LightPanelGeometry, TheBarEndsAreTheEndsOfTheRange) {
  const auto bar = warm().brightness.bar;
  EXPECT_EQ(light_panel::valueForX(bar, bar.x - 50, 0, 100), 0);
  EXPECT_EQ(light_panel::valueForX(bar, bar.x + bar.width + 50, 0, 100), 100);
  EXPECT_NEAR(light_panel::valueForX(bar, bar.x + bar.width / 2, 0, 100), 50, 2);
}

TEST(LightPanelGeometry, ANarrowScreenStillLeavesABarToDrag) {
  const auto layout = light_panel::forScreen(240, kLineHeight, /*hasWarmth=*/true);
  EXPECT_GT(layout.brightness.bar.width, 0);
  EXPECT_LE(layout.brightness.bar.x + layout.brightness.bar.width, 240);
}

}  // namespace
