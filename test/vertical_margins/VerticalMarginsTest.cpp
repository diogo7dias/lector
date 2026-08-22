#include "util/VerticalMargins.h"

#include <gtest/gtest.h>

using vertical_margins::Margins;

TEST(VerticalMarginsTest, LinkedEditMovesBothSides) {
  EXPECT_EQ(vertical_margins::setTop({10, 10}, 40, /*linked=*/true), (Margins{40, 40}));
  EXPECT_EQ(vertical_margins::setBottom({10, 10}, 40, /*linked=*/true), (Margins{40, 40}));
}

TEST(VerticalMarginsTest, UnlinkedEditLeavesTheOtherSide) {
  EXPECT_EQ(vertical_margins::setTop({10, 25}, 40, /*linked=*/false), (Margins{40, 25}));
  EXPECT_EQ(vertical_margins::setBottom({10, 25}, 40, /*linked=*/false), (Margins{10, 40}));
}

TEST(VerticalMarginsTest, RelinkingAdoptsTheTopValue) {
  EXPECT_EQ(vertical_margins::relink({40, 12}), (Margins{40, 40}));
}

// A file written with Uniform Margins on has no vertical values of its own: both sides
// must come from the horizontal margin, which is what the reader was already drawing.
TEST(VerticalMarginsTest, MigrationFromUniformOnTakesTheHorizontalMargin) {
  EXPECT_EQ(vertical_margins::migrateFromUniform(true, 18, {5, 5}), (Margins{18, 18}));
}

TEST(VerticalMarginsTest, MigrationFromUniformOffKeepsTheStoredSides) {
  EXPECT_EQ(vertical_margins::migrateFromUniform(false, 18, {40, 12}), (Margins{40, 12}));
}
