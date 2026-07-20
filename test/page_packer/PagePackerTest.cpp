#include <gtest/gtest.h>

#include "lib/Epub/Epub/parsers/PagePacker.h"

using pagepack::advanceY;
using pagepack::packElement;
using pagepack::PackDecision;

// --- Text lines (pre=0, post=0, cutIfNotEmpty=false) --------------------------

TEST(PagePackerTest, LineThatFitsPlacesInPlace) {
  const PackDecision d = packElement(/*nextY=*/100, /*empty=*/false, /*vh=*/800, 0, /*h=*/37, 0, false);
  EXPECT_FALSE(d.cutPage);
  EXPECT_EQ(d.yPos, 100);
  EXPECT_EQ(d.nextY, 137);
}

TEST(PagePackerTest, LineThatOverflowsBreaksToTopOfNewPage) {
  const PackDecision d = packElement(/*nextY=*/790, /*empty=*/false, /*vh=*/800, 0, 37, 0, false);
  EXPECT_TRUE(d.cutPage);
  EXPECT_EQ(d.yPos, 0);
  EXPECT_EQ(d.nextY, 37);
}

TEST(PagePackerTest, LineExactlyFillingPageDoesNotBreak) {
  // 763 + 37 == 800, and the test is strict `>` (not `>=`), so it still fits.
  const PackDecision d = packElement(/*nextY=*/763, /*empty=*/false, /*vh=*/800, 0, 37, 0, false);
  EXPECT_FALSE(d.cutPage);
  EXPECT_EQ(d.yPos, 763);
  EXPECT_EQ(d.nextY, 800);
}

TEST(PagePackerTest, LineOnePastFullBreaks) {
  // 764 + 37 == 801 > 800 -> break.
  const PackDecision d = packElement(/*nextY=*/764, /*empty=*/false, /*vh=*/800, 0, 37, 0, false);
  EXPECT_TRUE(d.cutPage);
  EXPECT_EQ(d.yPos, 0);
}

TEST(PagePackerTest, LineBreaksEvenOnEmptyPageWhenTallerThanViewport) {
  // cutIfNotEmpty=false: a line taller than the whole page still "breaks"
  // (matches the original addLineToPage having no !empty guard).
  const PackDecision d = packElement(/*nextY=*/0, /*empty=*/true, /*vh=*/30, 0, /*h=*/37, 0, false);
  EXPECT_TRUE(d.cutPage);
  EXPECT_EQ(d.yPos, 0);
  EXPECT_EQ(d.nextY, 37);
}

// --- Horizontal rules / images (pre/post spacing, cutIfNotEmpty=true) ---------

TEST(PagePackerTest, RuleBreaksOnNonEmptyPageUsingFullFootprint) {
  // pre=18, height=2, post=18 -> footprint 38. 790+38 > 800 -> break.
  const PackDecision d = packElement(/*nextY=*/790, /*empty=*/false, /*vh=*/800, 18, 2, 18, true);
  EXPECT_TRUE(d.cutPage);
  EXPECT_EQ(d.yPos, 18);          // 0 + pre
  EXPECT_EQ(d.nextY, 38);         // 18 + 2 + 18
}

TEST(PagePackerTest, RuleNeverOrphansOntoEmptyPage) {
  // cutIfNotEmpty=true + empty page: never break, even if it overflows.
  const PackDecision d = packElement(/*nextY=*/790, /*empty=*/true, /*vh=*/800, 18, 2, 18, true);
  EXPECT_FALSE(d.cutPage);
  EXPECT_EQ(d.yPos, 808);         // 790 + pre
  EXPECT_EQ(d.nextY, 828);        // 808 + 2 + 18
}

TEST(PagePackerTest, ImageThatFitsKeepsMargins) {
  const PackDecision d = packElement(/*nextY=*/100, /*empty=*/false, /*vh=*/800, /*mt=*/10, /*h=*/500, /*mb=*/10, true);
  EXPECT_FALSE(d.cutPage);
  EXPECT_EQ(d.yPos, 110);         // 100 + marginTop
  EXPECT_EQ(d.nextY, 620);        // 110 + 500 + 10
}

TEST(PagePackerTest, ImageThatOverflowsBreaksThenPlacesWithTopMargin) {
  const PackDecision d = packElement(/*nextY=*/400, /*empty=*/false, /*vh=*/800, 10, 500, 10, true);
  EXPECT_TRUE(d.cutPage);
  EXPECT_EQ(d.yPos, 10);          // 0 + marginTop
  EXPECT_EQ(d.nextY, 520);        // 10 + 500 + 10
}

// --- Pure vertical advance (block margins / paragraph gaps) -------------------

TEST(PagePackerTest, AdvanceYAddsWithoutBreaking) {
  EXPECT_EQ(advanceY(100, 18), 118);
  EXPECT_EQ(advanceY(0, 0), 0);
}
