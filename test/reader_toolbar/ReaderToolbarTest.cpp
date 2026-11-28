#include <gtest/gtest.h>

#include "activities/reader/ReaderToolbarSteps.h"

using namespace reader_toolbar;

TEST(ReaderToolbar, ToolFocusWrapsBothWays) {
  EXPECT_EQ(stepTool(Contents, 1), Bookmark);
  EXPECT_EQ(stepTool(Bookmark, 1), Text);
  EXPECT_EQ(stepTool(Text, 1), More);
  EXPECT_EQ(stepTool(More, 1), Contents);
  EXPECT_EQ(stepTool(Contents, -1), More);
  EXPECT_EQ(stepTool(Text, -1), Bookmark);
}

TEST(ReaderToolbar, ScrubLandsOnTheNearestSpineItem) {
  EXPECT_EQ(spineForPermille(0, 12), 0);
  EXPECT_EQ(spineForPermille(1000, 12), 11);
  EXPECT_EQ(spineForPermille(500, 11), 5);
  EXPECT_EQ(spineForPermille(-40, 12), 0);    // a drag past the left end
  EXPECT_EQ(spineForPermille(1400, 12), 11);  // and past the right end
  EXPECT_EQ(spineForPermille(700, 1), 0);     // one item: nowhere else to go
  EXPECT_EQ(spineForPermille(700, 0), 0);     // no spine at all
}

TEST(ReaderToolbar, ChapterStepsStayInsideTheBook) {
  EXPECT_EQ(clampSpine(-1, 5), 0);
  EXPECT_EQ(clampSpine(5, 5), 4);
  EXPECT_EQ(clampSpine(2, 5), 2);
}

TEST(ReaderToolbar, ShortSpinesScrubPagesInsteadOfChapters) {
  EXPECT_TRUE(scrubsPages(0));
  EXPECT_TRUE(scrubsPages(1));
  EXPECT_TRUE(scrubsPages(2));
  EXPECT_FALSE(scrubsPages(3));
  EXPECT_EQ(pageForPermille(0, 21), 0);
  EXPECT_EQ(pageForPermille(500, 21), 10);
  EXPECT_EQ(pageForPermille(1000, 21), 20);
  EXPECT_EQ(pageForPermille(-50, 21), 0);
  EXPECT_EQ(pageForPermille(1500, 21), 20);
  EXPECT_EQ(pageForPermille(500, 1), 0);
  EXPECT_EQ(clampPage(-1, 10), 0);
  EXPECT_EQ(clampPage(10, 10), 9);
  EXPECT_EQ(permilleForPage(0, 11), 0);
  EXPECT_EQ(permilleForPage(5, 11), 500);
  EXPECT_EQ(permilleForPage(10, 11), 1000);
  EXPECT_EQ(permilleForPage(0, 1), 0);
}
