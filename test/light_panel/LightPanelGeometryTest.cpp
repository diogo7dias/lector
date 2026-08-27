#include <gtest/gtest.h>

#include "components/LightPanelGeometry.h"

namespace {

constexpr int kScreenWidth = 480;
constexpr int kLineHeight = 25;

using light_panel::Hit;
using light_panel::Row;

constexpr int kReadoutLineHeight = 20;

light_panel::Layout inBook() {
  return light_panel::forScreen(kScreenWidth, kLineHeight, kReadoutLineHeight, /*hasWarmth=*/true, /*hasAux=*/true,
                                /*actionCount=*/4);
}
light_panel::Layout outOfBook() {
  return light_panel::forScreen(kScreenWidth, kLineHeight, kReadoutLineHeight, /*hasWarmth=*/true, /*hasAux=*/true,
                                /*actionCount=*/6);
}
light_panel::Layout plain() {
  return light_panel::forScreen(kScreenWidth, kLineHeight, kReadoutLineHeight, /*hasWarmth=*/false, /*hasAux=*/false,
                                /*actionCount=*/4);
}

int centerX(const light_panel::Rect& rect) { return rect.x + rect.width / 2; }
int centerY(const light_panel::Rect& rect) { return rect.y + rect.height / 2; }

TEST(LightPanelGeometry, ThePanelIsABandAcrossTheTopOfTheScreen) {
  const auto layout = inBook();
  EXPECT_EQ(layout.x, 0);
  EXPECT_EQ(layout.y, 0);
  EXPECT_EQ(layout.width, kScreenWidth);
  // A band, not a screen: it has to leave the page it is drawn over readable. Half of an
  // 800 px panel is the ceiling; the six-action form is the tallest one built.
  EXPECT_LT(outOfBook().height, 520);
}

TEST(LightPanelGeometry, TheToggleIsOneButtonAcrossTheFullWidth) {
  const auto layout = inBook();
  EXPECT_EQ(layout.toggle.x, light_panel::kSidePad);
  EXPECT_EQ(layout.toggle.width, kScreenWidth - light_panel::kSidePad * 2);
  EXPECT_GE(layout.toggle.height, light_panel::kStepRowHeight);
}

// The band reached the panel's physical top edge while its sides were inset 20 px, which
// read as the toggle being pushed up against the crop.
TEST(LightPanelGeometry, TheGapAboveTheToggleMatchesTheSideMargin) {
  EXPECT_EQ(inBook().toggle.y, light_panel::kSidePad);
}

TEST(LightPanelGeometry, EveryStepRowSharesOneColumnLayout) {
  const auto layout = inBook();
  for (const auto* row : {&layout.brightness, &layout.warmth, &layout.aux}) {
    // Both steppers live at the right end, in one column shared by every row.
    EXPECT_EQ(row->minus.x, layout.brightness.minus.x);
    EXPECT_EQ(row->plus.x, row->minus.x + row->minus.width + light_panel::kStepGap);
    EXPECT_EQ(row->minus.width, row->plus.width);
    EXPECT_EQ(row->plus.x + row->plus.width, kScreenWidth - light_panel::kSidePad);
    EXPECT_EQ(row->minus.y, row->plus.y);
    // Centred in the row rather than filling it: the row is the touch target, the box is
    // what is drawn.
    EXPECT_GT(row->minus.y, row->y);
    EXPECT_LT(row->minus.y + row->minus.height, row->y + row->height);
  }
}

TEST(LightPanelGeometry, ASliderRowReadsIconThenTrackThenNumberThenSteppers) {
  const auto layout = inBook();
  const auto& row = layout.brightness;
  EXPECT_EQ(row.icon.x, light_panel::kSidePad);
  EXPECT_GE(row.bar.x, row.icon.x + row.icon.width);
  EXPECT_GE(row.value.x, row.bar.x + row.bar.width);
  EXPECT_LE(row.value.x + row.value.width, row.minus.x);
  EXPECT_GE(row.bar.height, light_panel::kTrackHeight);
  EXPECT_EQ(layout.warmth.bar.x, row.bar.x);
  EXPECT_EQ(layout.warmth.bar.width, row.bar.width);
}

// The number belongs to the track it reports, not to the two buttons beside it.
TEST(LightPanelGeometry, TheNumberSitsCloserToTheTrackThanToTheSteppers) {
  const auto& row = inBook().brightness;
  const int toTrack = row.value.x - (row.bar.x + row.bar.width);
  const int toSteppers = row.minus.x - (row.value.x + row.value.width);
  EXPECT_LT(toTrack, toSteppers);
}

TEST(LightPanelGeometry, TheAuxRowHasSteppersButNoTrackOrIcon) {
  // Text Size in a book, Sort outside one: both step through values that no bar can show.
  const auto layout = inBook();
  EXPECT_TRUE(layout.hasAux);
  EXPECT_GT(layout.aux.minus.width, 0);
  EXPECT_EQ(layout.aux.bar.width, 0);
  EXPECT_EQ(layout.aux.icon.width, 0);
  // Its label takes the whole run up to the steppers, which sit in the sliders' column.
  EXPECT_EQ(layout.aux.value.x, light_panel::kSidePad);
  EXPECT_EQ(layout.aux.minus.x, layout.brightness.minus.x);
}

TEST(LightPanelGeometry, AWarmthlessBoardLosesTheRowRatherThanKeepingItEmpty) {
  const auto with = inBook();
  const auto without = plain();
  EXPECT_TRUE(with.hasWarmth);
  EXPECT_FALSE(without.hasWarmth);
  EXPECT_EQ(without.warmth.height, 0);
  EXPECT_LT(without.height, with.height);
}

TEST(LightPanelGeometry, RowsStackInsideTheBandWithoutOverlapping) {
  const auto layout = inBook();
  EXPECT_LE(layout.toggle.y + layout.toggle.height, layout.brightness.y);
  EXPECT_LE(layout.brightness.y + layout.brightness.height, layout.warmth.y);
  EXPECT_LE(layout.warmth.y + layout.warmth.height, layout.aux.y);
  EXPECT_LE(layout.aux.y + layout.aux.height, layout.actions[0].y);
  EXPECT_LE(layout.readout.y + layout.readout.height, layout.height);
}

TEST(LightPanelGeometry, ActionsFillTwoColumnsAndAsManyRowsAsTheyNeed) {
  const auto layout = outOfBook();
  ASSERT_EQ(layout.actionCount, 6);
  EXPECT_EQ(layout.actions[0].y, layout.actions[1].y);
  EXPECT_LT(layout.actions[0].x, layout.actions[1].x);
  EXPECT_GT(layout.actions[2].y, layout.actions[0].y);
  EXPECT_GT(layout.actions[4].y, layout.actions[2].y);
  for (int i = 0; i < layout.actionCount; ++i) {
    EXPECT_GE(layout.actions[i].x, light_panel::kSidePad);
    EXPECT_LE(layout.actions[i].x + layout.actions[i].width, kScreenWidth - light_panel::kSidePad);
  }
  EXPECT_GT(layout.height, inBook().height);
}

TEST(LightPanelGeometry, TheReadoutLineIsTheLastThingInTheBand) {
  const auto layout = outOfBook();
  const auto& last = layout.actions[layout.actionCount - 1];
  EXPECT_GE(layout.readout.y, last.y + last.height);
  EXPECT_EQ(layout.readout.width, kScreenWidth - light_panel::kSidePad * 2);
  // Set from the readout's own smaller font, not from the font the controls use.
  EXPECT_EQ(layout.readout.height, kReadoutLineHeight);
}

// --- Hit testing ---

TEST(LightPanelGeometry, ATouchOnTheToggleIsTheToggle) {
  const auto layout = inBook();
  const auto hit = light_panel::hitTest(layout, centerX(layout.toggle), centerY(layout.toggle));
  EXPECT_EQ(hit.kind, Hit::Kind::Toggle);
}

TEST(LightPanelGeometry, AStepperReportsItsRowAndItsDirection) {
  const auto layout = inBook();
  const auto minus = light_panel::hitTest(layout, centerX(layout.warmth.minus), centerY(layout.warmth.minus));
  EXPECT_EQ(minus.kind, Hit::Kind::Step);
  EXPECT_EQ(minus.row, Row::Warmth);
  EXPECT_EQ(minus.delta, -1);
  const auto plus = light_panel::hitTest(layout, centerX(layout.aux.plus), centerY(layout.aux.plus));
  EXPECT_EQ(plus.kind, Hit::Kind::Step);
  EXPECT_EQ(plus.row, Row::Aux);
  EXPECT_EQ(plus.delta, 1);
}

TEST(LightPanelGeometry, ATouchOnTheTrackIsATrackTouch) {
  const auto layout = inBook();
  const auto hit = light_panel::hitTest(layout, centerX(layout.brightness.bar), centerY(layout.brightness.bar));
  EXPECT_EQ(hit.kind, Hit::Kind::Track);
  EXPECT_EQ(hit.row, Row::Brightness);
}

// The old panel let a touch anywhere in the row set the value, and valueForX clamps
// everything left of the bar to 0 — so touching the word "Brightness" drove the light out.
TEST(LightPanelGeometry, TouchingTheRowBesideTheTrackDoesNotMoveTheValue) {
  const auto layout = inBook();
  const int aboveTrack = layout.brightness.y + 1;
  const auto hit = light_panel::hitTest(layout, centerX(layout.brightness.bar), aboveTrack);
  EXPECT_NE(hit.kind, Hit::Kind::Track);
}

TEST(LightPanelGeometry, ATouchOnAnActionReportsWhichOne) {
  const auto layout = outOfBook();
  for (int i = 0; i < layout.actionCount; ++i) {
    const auto hit = light_panel::hitTest(layout, centerX(layout.actions[i]), centerY(layout.actions[i]));
    EXPECT_EQ(hit.kind, Hit::Kind::Action);
    EXPECT_EQ(hit.action, i);
  }
}

TEST(LightPanelGeometry, ATouchBelowTheBandBelongsToThePageNotThePanel) {
  const auto layout = inBook();
  const int below = layout.y + layout.height + 1;
  EXPECT_FALSE(light_panel::insidePanel(layout, 10, below));
  EXPECT_EQ(light_panel::hitTest(layout, 10, below).kind, Hit::Kind::None);
  EXPECT_TRUE(light_panel::insidePanel(layout, 10, layout.y + 1));
}

TEST(LightPanelGeometry, TheWarmthRowIsUnreachableWhenTheBoardHasNoWarmth) {
  const auto layout = plain();
  for (int y = layout.y; y < layout.y + layout.height; ++y) {
    EXPECT_NE(light_panel::hitTest(layout, light_panel::kSidePad + 2, y).row, Row::Warmth);
  }
}

TEST(LightPanelGeometry, TheBarEndsAreTheEndsOfTheRange) {
  const auto bar = inBook().brightness.bar;
  EXPECT_EQ(light_panel::valueForX(bar, bar.x - 50, 0, 100), 0);
  EXPECT_EQ(light_panel::valueForX(bar, bar.x + bar.width + 50, 0, 100), 100);
  EXPECT_NEAR(light_panel::valueForX(bar, bar.x + bar.width / 2, 0, 100), 50, 2);
}

TEST(LightPanelGeometry, ANarrowScreenNarrowsTheSteppersRatherThanLosingTheTrack) {
  const auto layout = light_panel::forScreen(240, kLineHeight, kReadoutLineHeight, /*hasWarmth=*/true,
                                             /*hasAux=*/true, /*actionCount=*/4);
  EXPECT_GE(layout.brightness.bar.width, light_panel::kMinBarWidth);
  EXPECT_GT(layout.brightness.minus.width, 0);
  EXPECT_LE(layout.brightness.plus.x + layout.brightness.plus.width, 240 - light_panel::kSidePad);
}

}  // namespace
