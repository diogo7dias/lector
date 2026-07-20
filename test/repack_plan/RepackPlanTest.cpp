#include <gtest/gtest.h>

#include "lib/Epub/Epub/parsers/RepackPlan.h"

using namespace repack;

static PackRecord line(uint16_t para = 0, uint16_t li = 0) {
  return PackRecord{ELEM_LINE, 0, 0, 0, false, false, false, para, li};
}

TEST(RepackPlanTest, EffectiveSpacingLineUsesLineHeight) {
  PackParams p{40, 800, 0};
  int pre, h, post;
  effectiveSpacing(line(), p, pre, h, post);
  EXPECT_EQ(pre, 0);
  EXPECT_EQ(h, 40);
  EXPECT_EQ(post, 0);
}

TEST(RepackPlanTest, EffectiveSpacingImageUsesFixedHeight) {
  PackParams p{40, 800, 0};
  PackRecord img{ELEM_IMAGE, 5, 120, 7, false, false, true, 0, 0};
  int pre, h, post;
  effectiveSpacing(img, p, pre, h, post);
  EXPECT_EQ(pre, 5);
  EXPECT_EQ(h, 120);
  EXPECT_EQ(post, 7);
}

TEST(RepackPlanTest, EffectiveSpacingExtraParaHalfAndParaSpacing) {
  PackParams p{40, 800, 50};  // 50% paragraph spacing
  PackRecord r{ELEM_LINE, 0, 0, 3, true, true, false, 0, 0};
  int pre, h, post;
  effectiveSpacing(r, p, pre, h, post);
  // post = 3 (fixed) + 40/2 (extra half) + 40*50/100 (para spacing) = 3 + 20 + 20
  EXPECT_EQ(post, 43);
}

TEST(RepackPlanTest, SingleLineOnFirstPage) {
  Planner pl(PackParams{40, 800, 0});
  Placement a = pl.place(line());
  EXPECT_EQ(a.page, 0);
  EXPECT_EQ(a.yPos, 0);
  EXPECT_EQ(pl.pageCount(), 1);
}

TEST(RepackPlanTest, LinesFillThenOverflowToNextPage) {
  // viewport 100, lineHeight 40 => 2 lines fit (0..40, 40..80); the 3rd (80..120) overflows.
  Planner pl(PackParams{40, 100, 0});
  EXPECT_EQ(pl.place(line()).page, 0);  // yPos 0
  EXPECT_EQ(pl.place(line()).page, 0);  // yPos 40
  Placement third = pl.place(line());
  EXPECT_EQ(third.page, 1);
  EXPECT_EQ(third.yPos, 0);
  EXPECT_EQ(pl.pageCount(), 2);
}

TEST(RepackPlanTest, LineSpacingChangesLinesPerPage) {
  // Same 5 lines, viewport 100. At lineHeight 40 -> 2/page; at lineHeight 20 -> 5/page.
  {
    Planner tight(PackParams{20, 100, 0});
    for (int i = 0; i < 5; i++) tight.place(line());
    EXPECT_EQ(tight.pageCount(), 1);
  }
  {
    Planner loose(PackParams{40, 100, 0});
    for (int i = 0; i < 5; i++) loose.place(line());
    EXPECT_EQ(loose.pageCount(), 3);  // 2 + 2 + 1
  }
}

TEST(RepackPlanTest, BlockPreSpacingShiftsYPos) {
  Planner pl(PackParams{40, 800, 0});
  PackRecord firstLineOfBlock{ELEM_LINE, 15, 0, 0, false, false, false, 0, 0};  // 15px marginTop
  Placement a = pl.place(firstLineOfBlock);
  EXPECT_EQ(a.yPos, 15);  // pre pushes the line down
}

TEST(RepackPlanTest, ImageNeverOrphansEmptyPage) {
  // An image taller than the viewport on an EMPTY page must NOT cut (cutIfNotEmpty).
  Planner pl(PackParams{40, 100, 0});
  PackRecord tallImg{ELEM_IMAGE, 0, 500, 0, false, false, /*cutIfNotEmpty=*/true, 0, 0};
  Placement a = pl.place(tallImg);
  EXPECT_EQ(a.page, 0);  // stays on the empty first page rather than looping
}

TEST(RepackPlanTest, PerPageParagraphIndexIsLastWriter) {
  // viewport 100, lineHeight 40 => 2 lines per page.
  Planner pl(PackParams{40, 100, 0});
  pl.place(line(3, 1));           // page 0
  pl.place(line(4, 1));           // page 0 (last on page 0: para 4)
  Placement p1 = pl.place(line(5, 2));  // page 1
  EXPECT_EQ(p1.page, 1);
  EXPECT_EQ(pl.pageParagraphIndex(), 5);
  EXPECT_EQ(pl.pageListItemIndex(), 2);
}
