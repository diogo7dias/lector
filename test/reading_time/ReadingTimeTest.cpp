#include <gtest/gtest.h>

#include "ReadingTime.h"

using namespace reading_time;

TEST(ReadingTime, NoEstimateUntilThreePagesAreTimed) {
  PageTimer t;
  t.onTurn(0, true);  // first turn only sets the anchor
  t.onTurn(30000, true);
  t.onTurn(60000, true);
  EXPECT_EQ(-1, t.msPerPage());
  t.onTurn(90000, true);
  EXPECT_EQ(30000, t.msPerPage());
}

TEST(ReadingTime, FlicksIdleGapsAndBackTurnsAreNotSamples) {
  PageTimer t;
  t.onTurn(0, true);
  t.onTurn(500, true);                    // a flick: too fast
  t.onTurn(500 + MAX_PAGE_MS + 1, true);  // left open: too slow
  const uint32_t base = 500 + MAX_PAGE_MS + 1;
  t.onTurn(base + 20000, false);  // a back turn moves the anchor, adds nothing
  EXPECT_EQ(0, t.samples);
  t.onTurn(base + 40000, true);  // timed from the back turn, not before it
  EXPECT_EQ(1, t.samples);
  EXPECT_EQ(20000u, t.totalMs);
}

TEST(ReadingTime, MinutesRoundUpAndUnknownStaysUnknown) {
  EXPECT_EQ(-1, minutesFor(10, -1));
  EXPECT_EQ(0, minutesFor(0, 30000));
  EXPECT_EQ(1, minutesFor(1, 30000));  // half a minute shows as 1, never 0
  EXPECT_EQ(5, minutesFor(10, 30000));
  EXPECT_EQ(6, minutesFor(11, 30000));
}

TEST(ReadingTime, BookPagesLeftScalesTheRestByThisChapter) {
  // A 20-page chapter spanning 10% of the book, ending at 30%: 70% left = 140 pages, plus 5 here.
  EXPECT_EQ(145, bookPagesLeft(5, 20, 0.2f, 0.3f));
  EXPECT_EQ(5, bookPagesLeft(5, 20, 0.9f, 1.0f));   // last chapter: only what is left in it
  EXPECT_EQ(-1, bookPagesLeft(5, 20, 0.3f, 0.3f));  // no span to scale from
  EXPECT_EQ(-1, bookPagesLeft(0, 0, 0.2f, 0.3f));
}
