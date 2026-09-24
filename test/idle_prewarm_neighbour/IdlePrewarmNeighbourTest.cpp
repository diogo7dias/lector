#include <gtest/gtest.h>

#include "IdlePrewarmNeighbour.h"

TEST(IdlePrewarmNeighbour, FirstOpenPrewarmsTheNextPage) { EXPECT_EQ(idlePrewarmNeighbour(0, -1, -1, 0, 10), 1); }

TEST(IdlePrewarmNeighbour, ForwardMotionPrewarmsTheNextPage) { EXPECT_EQ(idlePrewarmNeighbour(6, 5, 0, 0, 10), 7); }

TEST(IdlePrewarmNeighbour, BackwardMotionPrewarmsThePreviousPage) {
  EXPECT_EQ(idlePrewarmNeighbour(5, 6, 0, 0, 10), 4);
}

TEST(IdlePrewarmNeighbour, BackwardFromTheFirstPageHasNoNeighbour) {
  EXPECT_EQ(idlePrewarmNeighbour(0, 1, 0, 0, 10), -1);
}

TEST(IdlePrewarmNeighbour, ForwardFromTheLastPageHasNoNeighbour) {
  EXPECT_EQ(idlePrewarmNeighbour(9, 8, 0, 0, 10), -1);
}

TEST(IdlePrewarmNeighbour, ASpineChangeStaysForwardEvenIfThePageNumberFell) {
  EXPECT_EQ(idlePrewarmNeighbour(0, 12, 2, 3, 8), 1);
}

TEST(IdlePrewarmNeighbour, AOnePageSectionHasNoNeighbour) { EXPECT_EQ(idlePrewarmNeighbour(0, -1, -1, 0, 1), -1); }
