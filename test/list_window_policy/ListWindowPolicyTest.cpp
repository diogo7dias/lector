#include <gtest/gtest.h>

#include "components/ListWindowPolicy.h"

using list_window::FrameSnapshot;
using list_window::planSelectionWindow;
using list_window::WindowRect;

namespace {

// A representative settings-style list: full-width, 40px rows, 10 per page.
FrameSnapshot baseFrame() {
  FrameSnapshot s;
  s.x = 0;
  s.y = 60;
  s.width = 480;
  s.height = 400;
  s.rowHeight = 40;
  s.pageItems = 10;
  s.itemCount = 25;
  s.selectedIndex = 3;
  s.extraHash = 0x1234;
  s.listDrawCalls = 1;
  s.valid = true;
  return s;
}

TEST(ListWindowPolicy, SelectionMoveDownOneRowWindowsBothRows) {
  const FrameSnapshot prev = baseFrame();
  FrameSnapshot cur = baseFrame();
  cur.selectedIndex = 4;

  WindowRect w{};
  ASSERT_TRUE(planSelectionWindow(prev, cur, &w));
  EXPECT_EQ(w.x, 0);
  EXPECT_EQ(w.y, 60 + 3 * 40 - 2);  // top of the higher row, incl. 2px selection overhang
  EXPECT_EQ(w.width, 480);
  EXPECT_EQ(w.height, 2 * 40 + 4);  // two rows plus overhang on both ends
}

TEST(ListWindowPolicy, SelectionMoveUpProducesSameUnionAsDown) {
  FrameSnapshot prev = baseFrame();
  prev.selectedIndex = 4;
  FrameSnapshot cur = baseFrame();
  cur.selectedIndex = 3;

  WindowRect w{};
  ASSERT_TRUE(planSelectionWindow(prev, cur, &w));
  EXPECT_EQ(w.y, 60 + 3 * 40 - 2);
  EXPECT_EQ(w.height, 2 * 40 + 4);
}

TEST(ListWindowPolicy, NonAdjacentMoveSpansAllRowsBetween) {
  const FrameSnapshot prev = baseFrame();  // row 3
  FrameSnapshot cur = baseFrame();
  cur.selectedIndex = 8;

  WindowRect w{};
  ASSERT_TRUE(planSelectionWindow(prev, cur, &w));
  EXPECT_EQ(w.y, 60 + 3 * 40 - 2);
  EXPECT_EQ(w.height, 6 * 40 + 4);
}

TEST(ListWindowPolicy, PageChangeRejected) {
  const FrameSnapshot prev = baseFrame();  // index 3, page 0
  FrameSnapshot cur = baseFrame();
  cur.selectedIndex = 13;  // page 1

  WindowRect w{};
  EXPECT_FALSE(planSelectionWindow(prev, cur, &w));
}

TEST(ListWindowPolicy, SameSelectionRejected) {
  const FrameSnapshot prev = baseFrame();
  const FrameSnapshot cur = baseFrame();
  WindowRect w{};
  EXPECT_FALSE(planSelectionWindow(prev, cur, &w));
}

TEST(ListWindowPolicy, ContentChangeRejectedViaExtraHash) {
  const FrameSnapshot prev = baseFrame();
  FrameSnapshot cur = baseFrame();
  cur.selectedIndex = 4;
  cur.extraHash = 0x9999;  // e.g. different folder path or hint label
  WindowRect w{};
  EXPECT_FALSE(planSelectionWindow(prev, cur, &w));
}

TEST(ListWindowPolicy, ItemCountChangeRejected) {
  const FrameSnapshot prev = baseFrame();
  FrameSnapshot cur = baseFrame();
  cur.selectedIndex = 4;
  cur.itemCount = 24;  // a row vanished — every row shifts
  WindowRect w{};
  EXPECT_FALSE(planSelectionWindow(prev, cur, &w));
}

TEST(ListWindowPolicy, GeometryChangeRejected) {
  const FrameSnapshot prev = baseFrame();
  FrameSnapshot cur = baseFrame();
  cur.selectedIndex = 4;
  cur.y = 80;
  WindowRect w{};
  EXPECT_FALSE(planSelectionWindow(prev, cur, &w));
}

TEST(ListWindowPolicy, FirstFrameRejected) {
  FrameSnapshot prev;  // invalid — nothing rendered yet
  FrameSnapshot cur = baseFrame();
  cur.selectedIndex = 4;
  WindowRect w{};
  EXPECT_FALSE(planSelectionWindow(prev, cur, &w));
}

TEST(ListWindowPolicy, MultipleListsInFrameRejected) {
  FrameSnapshot prev = baseFrame();
  FrameSnapshot cur = baseFrame();
  cur.selectedIndex = 4;
  cur.listDrawCalls = 2;
  WindowRect w{};
  EXPECT_FALSE(planSelectionWindow(prev, cur, &w));
  prev.listDrawCalls = 2;
  cur.listDrawCalls = 1;
  EXPECT_FALSE(planSelectionWindow(prev, cur, &w));
}

TEST(ListWindowPolicy, SelectionOutOfRangeRejected) {
  const FrameSnapshot prev = baseFrame();
  FrameSnapshot cur = baseFrame();
  cur.selectedIndex = 25;  // == itemCount, off the end
  WindowRect w{};
  EXPECT_FALSE(planSelectionWindow(prev, cur, &w));

  FrameSnapshot cur2 = baseFrame();
  cur2.selectedIndex = -1;
  EXPECT_FALSE(planSelectionWindow(prev, cur2, &w));
}

TEST(ListWindowPolicy, DegenerateRowGeometryRejected) {
  const FrameSnapshot prev = baseFrame();
  FrameSnapshot cur = baseFrame();
  cur.selectedIndex = 4;
  cur.rowHeight = 0;
  WindowRect w{};
  EXPECT_FALSE(planSelectionWindow(prev, cur, &w));
}

TEST(ListWindowPolicy, Hash32FoldsAndDiffers) {
  const uint32_t a = list_window::hash32("/books");
  const uint32_t b = list_window::hash32("/sleep");
  EXPECT_NE(a, b);
  // Folding a second string changes the value and depends on the seed.
  EXPECT_NE(list_window::hash32("Open", a), list_window::hash32("Open", b));
  // Null and empty behave identically and are stable.
  EXPECT_EQ(list_window::hash32(nullptr), list_window::hash32(""));
}

}  // namespace
