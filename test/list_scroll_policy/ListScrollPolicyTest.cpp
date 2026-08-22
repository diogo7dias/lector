#include <gtest/gtest.h>

#include "components/ListScrollPolicy.h"

using list_scroll::nextScrollOffset;

namespace {

// A settings-sized list: 22 rows against a 20-row window, as a stretch of the
// settings list looks once section headings are inserted.
constexpr int kPageItems = 20;
constexpr int kItemCount = 22;

TEST(ListScrollPolicy, ShortListNeverScrolls) {
  // Fewer items than the window: there is nothing below to reveal.
  EXPECT_EQ(nextScrollOffset(0, 0, kPageItems, 5), 0);
  EXPECT_EQ(nextScrollOffset(0, 4, kPageItems, 5), 0);
  // Even an offset that arrives dirty is pulled back to zero.
  EXPECT_EQ(nextScrollOffset(3, 4, kPageItems, 5), 0);
}

TEST(ListScrollPolicy, ExactFitNeverScrolls) {
  EXPECT_EQ(nextScrollOffset(0, kPageItems - 1, kPageItems, kPageItems), 0);
}

TEST(ListScrollPolicy, SelectionInsideWindowLeavesOffsetAlone) {
  // The whole point of scrolling over paging: moving within the visible window
  // must not move the window.
  for (int selected = 0; selected < kPageItems; ++selected) {
    EXPECT_EQ(nextScrollOffset(0, selected, kPageItems, kItemCount), 0) << "selected=" << selected;
  }
}

TEST(ListScrollPolicy, SteppingPastBottomSlidesByOneRow) {
  // Row 20 is the first row past a 20-row window, so the window advances by
  // exactly one and no more.
  EXPECT_EQ(nextScrollOffset(0, kPageItems, kPageItems, kItemCount), 1);
  EXPECT_EQ(nextScrollOffset(1, kPageItems + 1, kPageItems, kItemCount), 2);
}

TEST(ListScrollPolicy, SteppingAboveTopSlidesByOneRow) {
  EXPECT_EQ(nextScrollOffset(2, 1, kPageItems, kItemCount), 1);
  EXPECT_EQ(nextScrollOffset(1, 0, kPageItems, kItemCount), 0);
}

TEST(ListScrollPolicy, WindowStopsAtTheEndOfTheList) {
  // The last row sits at the bottom of the window; the window never runs on
  // past the list and leaves blank rows below.
  const int last = kItemCount - 1;
  EXPECT_EQ(nextScrollOffset(0, last, kPageItems, kItemCount), kItemCount - kPageItems);
  // An offset already beyond the end is clamped back.
  EXPECT_EQ(nextScrollOffset(99, last, kPageItems, kItemCount), kItemCount - kPageItems);
}

TEST(ListScrollPolicy, WrapToTopJumpsTheWindowBack) {
  // Next on the last row wraps the selection to the top of the ring; the
  // window has to follow in one move, not scroll back a row at a time.
  EXPECT_EQ(nextScrollOffset(kItemCount - kPageItems, 0, kPageItems, kItemCount), 0);
}

TEST(ListScrollPolicy, WrapToBottomJumpsTheWindowForward) {
  EXPECT_EQ(nextScrollOffset(0, kItemCount - 1, kPageItems, kItemCount), kItemCount - kPageItems);
}

TEST(ListScrollPolicy, NoSelectionKeepsTheWindowButStillClamps) {
  // EpubReaderMenuActivity draws lists with selectedIndex < 0.
  EXPECT_EQ(nextScrollOffset(1, -1, kPageItems, kItemCount), 1);
  EXPECT_EQ(nextScrollOffset(99, -1, kPageItems, kItemCount), kItemCount - kPageItems);
  EXPECT_EQ(nextScrollOffset(-4, -1, kPageItems, kItemCount), 0);
}

TEST(ListScrollPolicy, SelectionPastTheEndIsClampedNotTrusted) {
  // rebuildSettingsLists() can shrink the list under a stale selection.
  EXPECT_EQ(nextScrollOffset(0, 500, kPageItems, kItemCount), kItemCount - kPageItems);
}

TEST(ListScrollPolicy, DegenerateGeometryIsSafe) {
  EXPECT_EQ(nextScrollOffset(5, 3, 0, kItemCount), 0);
  EXPECT_EQ(nextScrollOffset(5, 3, -1, kItemCount), 0);
  EXPECT_EQ(nextScrollOffset(5, 3, kPageItems, 0), 0);
}

TEST(ListScrollPolicy, RepeatedCallsAreStable) {
  // Rendering the same frame twice must not creep the window.
  int offset = nextScrollOffset(0, kItemCount - 1, kPageItems, kItemCount);
  const int settled = offset;
  for (int i = 0; i < 5; ++i) {
    offset = nextScrollOffset(offset, kItemCount - 1, kPageItems, kItemCount);
  }
  EXPECT_EQ(offset, settled);
}

}  // namespace
