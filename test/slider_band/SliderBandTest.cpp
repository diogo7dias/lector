#include <gtest/gtest.h>

#include "components/SliderBandGeometry.h"

namespace {

constexpr int kScreenWidth = 480;
constexpr int kBandY = 8;
constexpr int kBandHeight = 45;
constexpr int kTextLineHeight = 20;

slider_band::Layout band() { return slider_band::forBand(0, kBandY, kScreenWidth, kBandHeight, kTextLineHeight); }

int centerX(const slider_band::Rect& rect) { return rect.x + rect.width / 2; }
int centerY(const slider_band::Rect& rect) { return rect.y + rect.height / 2; }

}  // namespace

TEST(SliderBandGeometry, EverythingStaysInsideTheBand) {
  const auto layout = band();
  ASSERT_TRUE(layout.valid);
  for (const slider_band::Rect& part : {layout.minus, layout.plus, layout.text, layout.track, layout.touch}) {
    EXPECT_GE(part.x, layout.band.x);
    EXPECT_LE(part.x + part.width, layout.band.x + layout.band.width);
    EXPECT_GE(part.y, layout.band.y);
    EXPECT_LE(part.y + part.height, layout.band.y + layout.band.height);
  }
}

TEST(SliderBandGeometry, TheButtonsSitAtTheEndsAndDoNotOverlapTheTrack) {
  const auto layout = band();
  EXPECT_LT(layout.minus.x + layout.minus.width, layout.track.x);
  EXPECT_GT(layout.plus.x, layout.track.x + layout.track.width);
  EXPECT_EQ(layout.minus.width, layout.minus.height);
  EXPECT_EQ(layout.plus.width, layout.plus.height);
}

TEST(SliderBandGeometry, ABandTooShortForATrackIsRefused) {
  const auto layout = slider_band::forBand(0, kBandY, kScreenWidth, 20, kTextLineHeight);
  EXPECT_FALSE(layout.valid);
  EXPECT_EQ(slider_band::hitTest(layout, 100, 20), slider_band::Hit::None);
}

TEST(SliderBandGeometry, ANarrowBandIsRefusedRatherThanDrawnAsASliver) {
  const auto layout = slider_band::forBand(0, kBandY, 120, kBandHeight, kTextLineHeight);
  EXPECT_FALSE(layout.valid);
}

TEST(SliderBandHits, EachControlAnswersForItsOwnRect) {
  const auto layout = band();
  EXPECT_EQ(slider_band::hitTest(layout, centerX(layout.minus), centerY(layout.minus)), slider_band::Hit::Minus);
  EXPECT_EQ(slider_band::hitTest(layout, centerX(layout.plus), centerY(layout.plus)), slider_band::Hit::Plus);
  EXPECT_EQ(slider_band::hitTest(layout, centerX(layout.track), centerY(layout.track)), slider_band::Hit::Track);
}

TEST(SliderBandHits, TheStripUnderTheTextAnswersForTheTrack) {
  const auto layout = band();
  // A thumb that lands low, on the band's bottom edge rather than on the 10 px bar.
  const int lowY = layout.band.y + layout.band.height - 1;
  EXPECT_EQ(slider_band::hitTest(layout, centerX(layout.track), lowY), slider_band::Hit::Track);
}

TEST(SliderBandHits, TheNameAndValueLineIsNotATrackTouch) {
  const auto layout = band();
  EXPECT_EQ(slider_band::hitTest(layout, centerX(layout.text), layout.text.y), slider_band::Hit::None);
}

TEST(SliderBandValue, TheEndsOfTheTrackAreTheEndsOfTheRange) {
  const auto layout = band();
  EXPECT_EQ(slider_band::valueForX(layout, layout.track.x, 80, 200, 1), 80);
  EXPECT_EQ(slider_band::valueForX(layout, layout.track.x + layout.track.width - 1, 80, 200, 1), 200);
}

TEST(SliderBandValue, ATouchOffTheTrackClampsInsteadOfWrapping) {
  const auto layout = band();
  EXPECT_EQ(slider_band::valueForX(layout, layout.band.x - 50, 80, 200, 1), 80);
  EXPECT_EQ(slider_band::valueForX(layout, layout.band.x + layout.band.width + 50, 80, 200, 1), 200);
}

TEST(SliderBandValue, TheMiddleOfTheTrackIsTheMiddleOfTheRange) {
  const auto layout = band();
  EXPECT_EQ(slider_band::valueForX(layout, centerX(layout.track), 0, 100, 1), 50);
}

TEST(SliderBandValue, ADragOnlyProducesValuesTheButtonsCouldReach) {
  const auto layout = band();
  for (int x = layout.track.x; x < layout.track.x + layout.track.width; ++x) {
    const int value = slider_band::valueForX(layout, x, 0, 100, 5);
    EXPECT_EQ(value % 5, 0) << "x=" << x << " gave " << value;
  }
}

TEST(SliderBandValue, ASingleValuedRangeDoesNotDivideByZero) {
  const auto layout = band();
  EXPECT_EQ(slider_band::valueForX(layout, centerX(layout.track), 7, 7, 1), 7);
  EXPECT_EQ(slider_band::fillWidthFor(layout, 7, 7, 7), 0);
}

TEST(SliderBandFill, TheFillRunsFromEmptyToTheWholeTrack) {
  const auto layout = band();
  EXPECT_EQ(slider_band::fillWidthFor(layout, 80, 80, 200), 0);
  EXPECT_EQ(slider_band::fillWidthFor(layout, 200, 80, 200), layout.track.width);
  EXPECT_EQ(slider_band::fillWidthFor(layout, 140, 80, 200), layout.track.width / 2);
}

TEST(SliderBandFill, AValueOutsideTheRangeIsHeldAtTheEnds) {
  const auto layout = band();
  EXPECT_EQ(slider_band::fillWidthFor(layout, 10, 80, 200), 0);
  EXPECT_EQ(slider_band::fillWidthFor(layout, 900, 80, 200), layout.track.width);
}

TEST(SliderBandGeometry, TheHeaderBandStartsBelowTheTopPadding) {
  // Not at row 0: on a panel mounted behind a bezel the first rows are covered,
  // which is what the top padding exists to clear. A band drawn from 0 puts its
  // own title under the frame.
  const auto rect = slider_band::headerBandRect(kScreenWidth, 14, 45);
  EXPECT_EQ(rect.x, 0);
  EXPECT_EQ(rect.y, 14);
  EXPECT_EQ(rect.width, kScreenWidth);
  EXPECT_EQ(rect.height, 45);
  const auto layout = slider_band::forBand(rect.x, rect.y, rect.width, rect.height, kTextLineHeight);
  EXPECT_TRUE(layout.valid);
  // Everything it draws sits inside the band, so nothing lands in the padding.
  EXPECT_GE(layout.text.y, rect.y);
  EXPECT_LE(layout.track.y + layout.track.height, rect.y + rect.height);
}
