#include <gtest/gtest.h>

#include "components/HintBandGeometry.h"

namespace {

// The X4 Pro: 480 px wide portrait, 800 tall, 56 px touch hint band.
hint_band::Band touchBand() { return hint_band::Band{480, 800, 56, /*touch=*/true, /*isX3=*/false}; }
// An X4 on buttons only: the historic 106 x 40 boxes.
hint_band::Band buttonBand() { return hint_band::Band{480, 800, 40, /*touch=*/false, /*isX3=*/false}; }

TEST(HintBand, TouchSlotsTileTheFullWidthWithoutGaps) {
  const hint_band::Band band = touchBand();
  EXPECT_EQ(hint_band::slot(band, 0).x, 0);
  for (int i = 1; i < 4; ++i) {
    const hint_band::Slot previous = hint_band::slot(band, i - 1);
    EXPECT_EQ(hint_band::slot(band, i).x, previous.x + previous.width);
  }
  const hint_band::Slot last = hint_band::slot(band, 3);
  EXPECT_EQ(last.x + last.width, band.screenWidth);
}

TEST(HintBand, TouchSlotsSitAtTheBottomAndFillTheBand) {
  const hint_band::Slot first = hint_band::slot(touchBand(), 0);
  EXPECT_EQ(first.y, 800 - 56);
  EXPECT_EQ(first.height, 56);
}

TEST(HintBand, ButtonBoardKeepsTheHistoricBoxes) {
  const hint_band::Band band = buttonBand();
  const int expectedX[4] = {25, 130, 245, 350};
  for (int i = 0; i < 4; ++i) {
    const hint_band::Slot slot = hint_band::slot(band, i);
    EXPECT_EQ(slot.x, expectedX[i]);
    EXPECT_EQ(slot.width, 106);
    EXPECT_EQ(slot.height, 40);
    EXPECT_EQ(slot.y, 800 - 40);
  }
}

TEST(HintBand, ButtonBoardUsesTheWiderX3Positions) {
  const hint_band::Band band{528, 792, 40, /*touch=*/false, /*isX3=*/true};
  EXPECT_EQ(hint_band::slot(band, 0).x, 38);
  EXPECT_EQ(hint_band::slot(band, 3).x, 384);
}

TEST(HintBand, ATapInsideASlotNamesThatSlot) {
  const hint_band::Band band = touchBand();
  for (int i = 0; i < 4; ++i) {
    const hint_band::Slot slot = hint_band::slot(band, i);
    EXPECT_EQ(hint_band::fromPoint(band, slot.x + slot.width / 2, slot.y + slot.height / 2), i);
  }
}

TEST(HintBand, ATapAboveTheBandNamesNoSlot) {
  const hint_band::Band band = touchBand();
  EXPECT_EQ(hint_band::fromPoint(band, 240, 800 - 57), -1);
}

TEST(HintBand, ATapBetweenTheButtonBoxesNamesNoSlot) {
  const hint_band::Band band = buttonBand();
  // 131 px is the gap between the first box (25..131) and the second (130..236) — the
  // 106 px boxes do not tile, so a press there is not any hint.
  EXPECT_EQ(hint_band::fromPoint(band, 8, 800 - 20), -1);
}

TEST(HintBand, TheTouchBandClearsTheFingerTargetFloor) {
  const hint_band::Slot slot = hint_band::slot(touchBand(), 0);
  EXPECT_GE(slot.height, 48);
  EXPECT_GE(slot.width, 48);
}

}  // namespace

namespace {

TEST(HintBandTap, AnUnlabelledSlotAnswersToNoTap) {
  const hint_band::Band band = touchBand();
  const bool labelled[hint_band::kSlotCount] = {true, true, false, true};
  const hint_band::Slot empty = hint_band::slot(band, 2);
  EXPECT_EQ(hint_band::tappedSlot(band, empty.x + 4, empty.y + 4, labelled), -1);
}

TEST(HintBandTap, ALabelledSlotAnswersToATapInsideIt) {
  const hint_band::Band band = touchBand();
  const bool labelled[hint_band::kSlotCount] = {true, true, false, true};
  const hint_band::Slot third = hint_band::slot(band, 3);
  EXPECT_EQ(hint_band::tappedSlot(band, third.x + 4, third.y + 4, labelled), 3);
}

TEST(HintBandTap, ABoardWithoutTouchAnswersToNoTapAtAll) {
  hint_band::Band band = touchBand();
  band.touch = false;
  const bool labelled[hint_band::kSlotCount] = {true, true, true, true};
  const hint_band::Slot first = hint_band::slot(band, 0);
  EXPECT_EQ(hint_band::tappedSlot(band, first.x + 4, first.y + 4, labelled), -1);
}

}  // namespace
