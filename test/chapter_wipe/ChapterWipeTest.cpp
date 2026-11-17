#include <gtest/gtest.h>

#include <vector>

#include "activities/reader/ChapterWipe.h"

using namespace chapter_wipe;

TEST(ChapterWipe, DiagonalsStayOnScreen) {
  constexpr int W = 480, H = 800;
  int lines = 0;
  std::vector<int> inked(W * H, 0);
  forEachHatchLine(W, H, [&](int x1, int y1, const int x2, const int y2) {
    ++lines;
    ASSERT_TRUE(x1 >= 0 && x1 < W && x2 >= 0 && x2 < W);
    ASSERT_TRUE(y1 >= 0 && y1 < H && y2 >= 0 && y2 < H);
    // A 45-degree line: both axes move by the same amount.
    ASSERT_EQ(std::abs(x2 - x1), std::abs(y2 - y1));
    const int sy = y2 > y1 ? 1 : -1;
    for (int x = x1, y = y1; x <= x2; ++x, y += sy) inked[y * W + x] = 1;
  });
  EXPECT_GT(lines, 0);
  int ink = 0;
  for (const int p : inked) ink += p;
  // Two directions, STROKE of every SPACING diagonals each: about 2 * 2/12 of the screen,
  // less the crossings.
  EXPECT_GT(ink, W * H / 5);
  EXPECT_LT(ink, W * H * 2 / 5);
}

TEST(ChapterWipe, CornersAndMisses) {
  int x1, y1, x2, y2;
  ASSERT_TRUE(diagonal(0, false, 10, 5, x1, y1, x2, y2));  // the top-left corner pixel
  EXPECT_EQ(0, x1);
  EXPECT_EQ(0, y1);
  EXPECT_EQ(0, x2);
  EXPECT_EQ(0, y2);
  EXPECT_FALSE(diagonal(14, false, 10, 5, x1, y1, x2, y2));  // past the bottom-right corner
  ASSERT_TRUE(diagonal(-4, true, 10, 5, x1, y1, x2, y2));    // the bottom-left corner pixel
  EXPECT_EQ(0, x1);
  EXPECT_EQ(4, y1);
  EXPECT_FALSE(diagonal(10, true, 10, 5, x1, y1, x2, y2));
}
