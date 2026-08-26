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

// --- The two action buttons ---
//
// Sleep and Rotate sit below the sliders as a pair of text buttons, side by side. They
// are the only controls in the panel that do something other than change the light, and
// on a board with no touch the panel is unreachable anyway, so they are touch targets
// first: each takes half the width, which is a far larger target than a 12 px bar.

TEST(LightPanelGeometry, TheTwoButtonsSplitTheWidthBelowTheSliders) {
  const auto layout = warm();
  EXPECT_GT(layout.sleep.width, 0);
  EXPECT_EQ(layout.sleep.width, layout.rotate.width);
  EXPECT_EQ(layout.sleep.y, layout.rotate.y);
  EXPECT_GT(layout.sleep.y, layout.warmth.y);
  EXPECT_LT(layout.sleep.x, layout.rotate.x);
  // Inside the band, and clear of each other.
  EXPECT_GE(layout.sleep.x, 0);
  EXPECT_LE(layout.rotate.x + layout.rotate.width, layout.width);
  EXPECT_LE(layout.sleep.x + layout.sleep.width, layout.rotate.x);
  EXPECT_LE(layout.rotate.y + layout.rotate.height, layout.height);
}

TEST(LightPanelGeometry, TheButtonsFollowTheSlidersUpOnABoardWithoutWarmth) {
  const auto with = warm();
  const auto without = plain();
  EXPECT_LT(without.sleep.y, with.sleep.y);
  EXPECT_LE(without.rotate.y + without.rotate.height, without.height);
}

TEST(LightPanelGeometry, ATouchLandsOnTheButtonItIsOver) {
  const auto layout = warm();
  using light_panel::Button;
  EXPECT_EQ(light_panel::buttonAt(layout, layout.sleep.x + 2, layout.sleep.y + 2), Button::Sleep);
  EXPECT_EQ(light_panel::buttonAt(layout, layout.rotate.x + 2, layout.rotate.y + 2), Button::Rotate);
}

TEST(LightPanelGeometry, ATouchBetweenOrAboveTheButtonsHitsNeither) {
  const auto layout = warm();
  using light_panel::Button;
  const int gapX = (layout.sleep.x + layout.sleep.width + layout.rotate.x) / 2;
  EXPECT_EQ(light_panel::buttonAt(layout, gapX, layout.sleep.y + 2), Button::None);
  EXPECT_EQ(light_panel::buttonAt(layout, layout.sleep.x + 2, layout.brightness.y), Button::None);
}

TEST(LightPanelGeometry, TheButtonRowIsNotAlsoASliderRow) {
  // rowAt drives the drag handling, so the buttons must not read as Warmth and move it.
  const auto layout = warm();
  EXPECT_EQ(light_panel::rowAt(layout, layout.sleep.x + 2, layout.sleep.y + 2), light_panel::Row::None);
}

// The device drew "FrontlighOFF" and a bar over the end of "Brightness": the fixed 110 px
// label column was narrower than the strings the panel actually puts in it. The width is
// measured and passed in now, and the bar has to start clear of it.
TEST(LightPanelGeometry, TheBarStartsAfterTheMeasuredLabelColumn) {
  const auto layout = light_panel::forScreen(480, 22, /*hasWarmth=*/true, /*labelWidth=*/220);
  EXPECT_GE(layout.brightness.bar.x, light_panel::kSidePad + 220);
  EXPECT_EQ(layout.warmth.bar.x, layout.brightness.bar.x);
  EXPECT_GE(layout.brightness.bar.width, light_panel::kMinBarWidth);
  EXPECT_LE(layout.brightness.bar.x + layout.brightness.bar.width, 480 - light_panel::kSidePad);
}
