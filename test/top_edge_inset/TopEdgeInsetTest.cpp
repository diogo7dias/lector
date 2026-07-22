#include <gtest/gtest.h>

#include "src/components/TopEdgeInset.h"

TEST(TopEdgeInsetTest, X4ContentMovesBelowCroppedPanelEdge) { EXPECT_EQ(topEdgeInset(true), 2); }

TEST(TopEdgeInsetTest, X3LayoutStaysUnchanged) { EXPECT_EQ(topEdgeInset(false), 0); }
