#include <gtest/gtest.h>

#include "GfxRenderer/ViewableInsets.h"

using gfx::InsetOrientation;
using gfx::orientInsets;
using gfx::ViewableInsets;

// The X4 bezel overlaps the panel's first ~9 rows; the X3 bezel overlaps none.
constexpr ViewableInsets kX4{9, 3, 3, 3};
constexpr ViewableInsets kX3{0, 3, 3, 3};

TEST(ViewableInsetsTest, PortraitIsUnchanged) { EXPECT_EQ(orientInsets(kX4, InsetOrientation::Portrait), kX4); }

TEST(ViewableInsetsTest, RotationMovesTheCroppedEdge) {
  EXPECT_EQ(orientInsets(kX4, InsetOrientation::LandscapeClockwise), (ViewableInsets{3, 9, 3, 3}));
  EXPECT_EQ(orientInsets(kX4, InsetOrientation::PortraitInverted), (ViewableInsets{3, 3, 9, 3}));
  EXPECT_EQ(orientInsets(kX4, InsetOrientation::LandscapeCounterClockwise), (ViewableInsets{3, 3, 3, 9}));
}

// A board with no top crop keeps a zero top inset in every orientation the crop
// can rotate into, so the status bar draws flush against the screen edge.
TEST(ViewableInsetsTest, ABoardWithoutATopCropStaysFlush) {
  EXPECT_EQ(orientInsets(kX3, InsetOrientation::Portrait).top, 0);
  EXPECT_EQ(orientInsets(kX3, InsetOrientation::LandscapeClockwise).right, 0);
  EXPECT_EQ(orientInsets(kX3, InsetOrientation::PortraitInverted).bottom, 0);
  EXPECT_EQ(orientInsets(kX3, InsetOrientation::LandscapeCounterClockwise).left, 0);
}
