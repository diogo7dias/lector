#include <gtest/gtest.h>

#include "util/MarginLink.h"

using margin_link::Margins;
using margin_link::Mode;
using margin_link::State;

TEST(MarginLinkTest, LinkedEditMovesBothVerticalSides) {
  EXPECT_EQ(margin_link::setTop({5, 10, 10}, 40, Mode::TopBottom), (Margins{5, 40, 40}));
  EXPECT_EQ(margin_link::setBottom({5, 10, 10}, 40, Mode::TopBottom), (Margins{5, 40, 40}));
}

TEST(MarginLinkTest, SeparateEditLeavesTheOtherSide) {
  EXPECT_EQ(margin_link::setTop({5, 10, 25}, 40, Mode::Separate), (Margins{5, 40, 25}));
  EXPECT_EQ(margin_link::setBottom({5, 10, 25}, 40, Mode::Separate), (Margins{5, 10, 40}));
}

// The single Margin row shown in All Sides is the horizontal one, so editing it has to
// carry every side; otherwise the list would show one number and draw three.
TEST(MarginLinkTest, AllSidesEditMovesEverySide) {
  EXPECT_EQ(margin_link::setHorizontal({5, 10, 25}, 40, Mode::AllSides), (Margins{40, 40, 40}));
}

TEST(MarginLinkTest, HorizontalEditLeavesVerticalAloneOutsideAllSides) {
  EXPECT_EQ(margin_link::setHorizontal({5, 10, 25}, 40, Mode::TopBottom), (Margins{40, 10, 25}));
  EXPECT_EQ(margin_link::setHorizontal({5, 10, 25}, 40, Mode::Separate), (Margins{40, 10, 25}));
}

TEST(MarginLinkTest, LinkingTopBottomAdoptsTheTopValue) {
  const State next = margin_link::applyMode({{5, 40, 12}, /*dynamicMargins=*/2}, Mode::TopBottom);
  EXPECT_EQ(next.margins, (Margins{5, 40, 40}));
}

// Dynamic Margins drives the horizontal margin on its own, so leaving it on would make
// All Sides draw a left/right margin that does not match top/bottom.
TEST(MarginLinkTest, AllSidesAdoptsTheHorizontalMarginAndForcesDynamicOff) {
  const State next = margin_link::applyMode({{18, 40, 12}, /*dynamicMargins=*/2}, Mode::AllSides);
  EXPECT_EQ(next.margins, (Margins{18, 18, 18}));
  EXPECT_EQ(next.dynamicMargins, 0);
}

TEST(MarginLinkTest, LeavingAllSidesKeepsDynamicOff) {
  const State allSides = margin_link::applyMode({{18, 40, 12}, /*dynamicMargins=*/2}, Mode::AllSides);
  const State next = margin_link::applyMode(allSides, Mode::Separate);
  EXPECT_EQ(next.dynamicMargins, 0);
  EXPECT_EQ(next.margins, (Margins{18, 18, 18}));
}

TEST(MarginLinkTest, DynamicMarginsSurvivesModesOtherThanAllSides) {
  EXPECT_EQ(margin_link::applyMode({{5, 10, 25}, 2}, Mode::TopBottom).dynamicMargins, 2);
  EXPECT_EQ(margin_link::applyMode({{5, 10, 25}, 2}, Mode::Separate).dynamicMargins, 2);
}

TEST(MarginLinkTest, SeparatingLeavesBothStoredSides) {
  const State next = margin_link::applyMode({{5, 40, 40}, 0}, Mode::Separate);
  EXPECT_EQ(next.margins, (Margins{5, 40, 40}));
}

TEST(MarginLinkTest, ModeCyclesOffThenTopBottomThenAllSides) {
  EXPECT_EQ(margin_link::nextMode(Mode::Separate), Mode::TopBottom);
  EXPECT_EQ(margin_link::nextMode(Mode::TopBottom), Mode::AllSides);
  EXPECT_EQ(margin_link::nextMode(Mode::AllSides), Mode::Separate);
}

// A file written with Uniform Margins on has no vertical values of its own: both sides
// must come from the horizontal margin, which is what the reader was already drawing.
TEST(MarginLinkTest, MigrationFromUniformOnRestoresAllSides) {
  const State next = margin_link::migrateFromUniform(true, {18, 5, 5}, /*dynamicMargins=*/0);
  EXPECT_EQ(next.mode, Mode::AllSides);
  EXPECT_EQ(next.margins, (Margins{18, 18, 18}));
}

TEST(MarginLinkTest, MigrationFromUniformOffKeepsTheStoredSidesAndSeparates) {
  const State next = margin_link::migrateFromUniform(false, {18, 40, 12}, /*dynamicMargins=*/1);
  EXPECT_EQ(next.mode, Mode::Separate);
  EXPECT_EQ(next.margins, (Margins{18, 40, 12}));
  EXPECT_EQ(next.dynamicMargins, 1);
}

// Uniform Margins and Dynamic Margins could both be on in an old file; All Sides is the
// stronger statement, so the migrated file must not keep a dynamic margin it cannot use.
TEST(MarginLinkTest, MigrationFromUniformOnForcesDynamicOff) {
  EXPECT_EQ(margin_link::migrateFromUniform(true, {18, 5, 5}, /*dynamicMargins=*/2).dynamicMargins, 0);
}

TEST(MarginLinkTest, ModeStoredAsNumberRoundTrips) {
  EXPECT_EQ(margin_link::toMode(0), Mode::Separate);
  EXPECT_EQ(margin_link::toMode(1), Mode::TopBottom);
  EXPECT_EQ(margin_link::toMode(2), Mode::AllSides);
  // An out-of-range number in a hand-edited or corrupt file falls back to the default.
  EXPECT_EQ(margin_link::toMode(7), Mode::AllSides);
}
