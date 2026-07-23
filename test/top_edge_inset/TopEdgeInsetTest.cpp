#include <gtest/gtest.h>

#include "src/components/TopEdgeInset.h"

TEST(TopEdgeInsetTest, X4ContentMovesBelowCroppedPanelEdge) { EXPECT_EQ(topEdgeInset(true), 9); }

TEST(TopEdgeInsetTest, X3LayoutStaysUnchanged) { EXPECT_EQ(topEdgeInset(false), 0); }

// Chrome top origin: X3 unchanged, X4 shifted down by exactly the inset so the
// whole chrome layout (header + everything derived from topPadding) tracks together.
TEST(TopEdgeInsetTest, ChromeTopPaddingX3Unchanged) { EXPECT_EQ(chromeTopPadding(5, false), 5); }

TEST(TopEdgeInsetTest, ChromeTopPaddingX4ShiftedByInset) { EXPECT_EQ(chromeTopPadding(5, true), 5 + 9); }
