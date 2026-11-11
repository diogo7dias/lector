#include <gtest/gtest.h>

#include "activities/reader/QueuedTurns.h"

TEST(QueuedTurns, CountsPressesUpToFourEitherWay) {
  EXPECT_EQ(queueTurn(0, true), 1);
  EXPECT_EQ(queueTurn(3, true), 4);
  EXPECT_EQ(queueTurn(4, true), 4);
  EXPECT_EQ(queueTurn(0, false), -1);
  EXPECT_EQ(queueTurn(-4, false), -4);
}

TEST(QueuedTurns, AnOppositePressCancelsOne) {
  EXPECT_EQ(queueTurn(2, false), 1);
  EXPECT_EQ(queueTurn(-1, true), 0);
}

TEST(QueuedTurns, AllApplyWhileThePagesExist) {
  EXPECT_EQ(queuedTurnsNow(3, true, 2, 10), 3);
  EXPECT_EQ(queuedTurnsNow(3, false, 5, 10), 3);
}

// Page index 8 of 10: one turn lands on page 9, one leaves the chapter (or steps onto the
// page still to be built), and the other two are dropped.
TEST(QueuedTurns, OnlyOneTurnStepsPastTheLaidOutPages) {
  EXPECT_EQ(queuedTurnsNow(4, true, 8, 10), 2);
  EXPECT_EQ(queuedTurnsNow(4, true, 9, 10), 1);
  EXPECT_EQ(queuedTurnsNow(4, false, 2, 10), 3);
  EXPECT_EQ(queuedTurnsNow(4, false, 0, 10), 1);
}

TEST(QueuedTurns, AlreadyPastTheBuiltPagesStillTakesOne) { EXPECT_EQ(queuedTurnsNow(3, true, 10, 10), 1); }
