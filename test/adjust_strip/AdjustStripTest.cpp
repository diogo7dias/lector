#include <gtest/gtest.h>

#include "components/AdjustStrip.h"

namespace {
constexpr int kTop = 380;
constexpr int kLine = 25;

bool inside(const adjust_strip::Rect& outer, const adjust_strip::Rect& inner) {
  return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.width <= outer.x + outer.width &&
         inner.y + inner.height <= outer.y + outer.height;
}
}  // namespace

TEST(AdjustStrip, EveryPartSitsInsideTheStripAtBothPanelWidths) {
  for (const int width : {480, 528, 800}) {
    const auto strip = adjust_strip::forStrip(kTop, width, kLine);
    EXPECT_EQ(strip.frame.height, adjust_strip::heightFor(kLine));
    for (const auto& part : {strip.label, strip.row, strip.track, strip.minus, strip.plus}) {
      EXPECT_TRUE(inside(strip.frame, part)) << width;
    }
    EXPECT_GT(strip.track.width, 200) << "the track stays worth dragging on " << width;
  }
}

TEST(AdjustStrip, TrackMinusAndPlusDoNotOverlap) {
  const auto strip = adjust_strip::forStrip(kTop, 480, kLine);
  EXPECT_LT(strip.track.x + strip.track.width, strip.minus.x);
  EXPECT_LT(strip.minus.x + strip.minus.width, strip.plus.x);
  EXPECT_LE(strip.row.x + strip.row.width, strip.minus.x);
}

TEST(AdjustStrip, ATouchAnywhereOnTheRowLeftOfTheMinusLandsOnTheTrack) {
  const auto strip = adjust_strip::forStrip(kTop, 480, kLine);
  EXPECT_EQ(adjust_strip::hitTest(strip, 2, strip.row.y + 1), adjust_strip::Hit::Track);
  EXPECT_EQ(adjust_strip::hitTest(strip, strip.track.x + 10, strip.row.y + strip.row.height - 1),
            adjust_strip::Hit::Track);
  EXPECT_EQ(adjust_strip::hitTest(strip, strip.minus.x + 5, strip.minus.y + 5), adjust_strip::Hit::Minus);
  EXPECT_EQ(adjust_strip::hitTest(strip, strip.plus.x + 5, strip.plus.y + 5), adjust_strip::Hit::Plus);
  EXPECT_EQ(adjust_strip::hitTest(strip, 100, strip.label.y + 2), adjust_strip::Hit::Strip);
  EXPECT_EQ(adjust_strip::hitTest(strip, 100, kTop - 1), adjust_strip::Hit::None);
  EXPECT_EQ(adjust_strip::hitTest(strip, 100, kTop + strip.frame.height), adjust_strip::Hit::None);
}
