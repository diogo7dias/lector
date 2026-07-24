#include <gtest/gtest.h>

#include "StatusBar.h"

using statusbar::BarLayout;
using statusbar::clusterWidth;
using statusbar::kMaxPerAnchor;
using statusbar::reflowTitle;
using statusbar::Seg;

namespace {

constexpr int kBand = 300;  // band text width used across the suite
constexpr int kSep = 9;     // separator advance (gap4 + bar1 + gap4)

// Anchors: 0=TL 1=TC 2=TR 3=BL 4=BC 5=BR
constexpr int TL = 0, TC = 1, TR = 2, BL = 3, BC = 4, BR = 5;

// Give an anchor a single segment of the given width.
void put(BarLayout& l, int anchor, int width) { l.buckets[anchor][l.counts[anchor]++] = Seg{"x", width, false}; }

int total(const BarLayout& l) {
  int n = 0;
  for (int a = 0; a < statusbar::kAnchorCount; a++) n += l.counts[a];
  return n;
}

}  // namespace

TEST(StatusBarReflow, ClusterWidthSumsSegmentsAndSeparators) {
  BarLayout l;
  put(l, TC, 10);
  put(l, TC, 20);
  put(l, TC, 30);
  EXPECT_EQ(clusterWidth(l, TC, kSep), 10 + 20 + 30 + kSep * 2);
  EXPECT_EQ(clusterWidth(l, TL, kSep), 0);  // empty anchor
}

TEST(StatusBarReflow, NoTitleIsNoOp) {
  BarLayout l;
  put(l, BL, 40);
  put(l, BR, 40);
  reflowTitle(l, /*titleAnchor=*/-1, /*truncate=*/false, kBand, kSep, /*destReserved=*/true);
  EXPECT_EQ(l.counts[BL], 1);
  EXPECT_EQ(l.counts[BR], 1);
}

TEST(StatusBarReflow, TruncatingTitleNeverReflows) {
  BarLayout l;
  put(l, BC, 240);  // wide title, would overlap both corners
  put(l, BL, 40);
  put(l, BR, 40);
  reflowTitle(l, BC, /*truncate=*/true, kBand, kSep, /*destReserved=*/true);
  EXPECT_EQ(l.counts[BL], 1);
  EXPECT_EQ(l.counts[BR], 1);
  EXPECT_EQ(l.counts[TL], 0);
}

TEST(StatusBarReflow, ShortTitleDoesNotBumpNeighbours) {
  BarLayout l;
  put(l, BC, 100);  // centred [100,200], clears both corners
  put(l, BL, 40);
  put(l, BR, 40);
  reflowTitle(l, BC, false, kBand, kSep, true);
  EXPECT_EQ(l.counts[BL], 1);
  EXPECT_EQ(l.counts[BR], 1);
  EXPECT_EQ(l.counts[TL], 0);
  EXPECT_EQ(l.counts[TR], 0);
}

TEST(StatusBarReflow, CentreTitleBumpsBothCornersToFreeOppositeAnchors) {
  BarLayout l;
  put(l, BC, 240);  // centred [30,270] overlaps both corners
  put(l, BL, 40);
  put(l, BR, 40);
  reflowTitle(l, BC, false, kBand, kSep, /*destReserved=*/true);
  EXPECT_EQ(l.counts[BC], 1);  // title stays
  EXPECT_EQ(l.counts[BL], 0);  // left corner left
  EXPECT_EQ(l.counts[BR], 0);  // right corner left
  EXPECT_EQ(l.counts[TL], 1);  // sat same-side (left -> top-left)
  EXPECT_EQ(l.counts[TR], 1);  // sat same-side (right -> top-right)
}

TEST(StatusBarReflow, BumpedItemsHideWhenOppositeBandUnreserved) {
  BarLayout l;
  put(l, BC, 240);
  put(l, BL, 40);
  put(l, BR, 40);
  reflowTitle(l, BC, false, kBand, kSep, /*destReserved=*/false);
  EXPECT_EQ(l.counts[BC], 1);  // title stays
  EXPECT_EQ(l.counts[BL], 0);  // corners hidden, not relocated
  EXPECT_EQ(l.counts[BR], 0);
  EXPECT_EQ(l.counts[TL], 0);
  EXPECT_EQ(l.counts[TR], 0);
  EXPECT_EQ(total(l), 1);
}

TEST(StatusBarReflow, TopCornerTitleBumpsNeighboursDown) {
  BarLayout l;
  put(l, TL, 280);  // left-aligned [0,280] overlaps TC and TR
  put(l, TC, 40);
  put(l, TR, 40);
  reflowTitle(l, TL, false, kBand, kSep, /*destReserved=*/true);
  EXPECT_EQ(l.counts[TL], 1);  // title stays
  EXPECT_EQ(l.counts[TC], 0);
  EXPECT_EQ(l.counts[TR], 0);
  // centre falls to opposite centre; right falls to opposite right
  EXPECT_EQ(l.counts[BC], 1);
  EXPECT_EQ(l.counts[BR], 1);
}

TEST(StatusBarReflow, BumpedItemJoinsOccupiedRoomyAnchor) {
  BarLayout l;
  put(l, TC, 240);  // centred [30,270] overlaps TL and TR
  put(l, TL, 40);
  put(l, TR, 40);
  put(l, BL, 30);  // opposite band pre-occupied but roomy
  put(l, BC, 30);
  put(l, BR, 30);
  reflowTitle(l, TC, false, kBand, kSep, /*destReserved=*/true);
  EXPECT_EQ(l.counts[TC], 1);
  EXPECT_EQ(l.counts[TL], 0);
  EXPECT_EQ(l.counts[TR], 0);
  EXPECT_EQ(l.counts[BL], 2);  // TL joined here
  EXPECT_EQ(l.counts[BR], 2);  // TR joined here
  EXPECT_EQ(l.counts[BC], 1);  // untouched
}

TEST(StatusBarReflow, BumpedItemHiddenWhenOppositeBandFull) {
  BarLayout l;
  put(l, TC, 290);  // near-full centred [5,295] overlaps both corners
  put(l, TL, 40);
  put(l, TR, 40);
  put(l, BL, 280);  // every opposite anchor occupied and too wide to join
  put(l, BC, 280);
  put(l, BR, 280);
  reflowTitle(l, TC, false, kBand, kSep, /*destReserved=*/true);
  EXPECT_EQ(l.counts[TC], 1);
  EXPECT_EQ(l.counts[TL], 0);  // hidden
  EXPECT_EQ(l.counts[TR], 0);  // hidden
  EXPECT_EQ(l.counts[BL], 1);  // opposite band unchanged
  EXPECT_EQ(l.counts[BC], 1);
  EXPECT_EQ(l.counts[BR], 1);
}

TEST(StatusBarReflow, JoinRespectsBucketCapacity) {
  BarLayout l;
  put(l, TC, 240);
  put(l, TR, 40);                                          // one overlapping neighbour on the right
  for (int i = 0; i < kMaxPerAnchor; i++) put(l, BR, 10);  // dest already full
  reflowTitle(l, TC, false, kBand, kSep, /*destReserved=*/true);
  // BR is full and narrow siblings BC/BL are empty -> right item falls to BC.
  EXPECT_EQ(l.counts[TR], 0);
  EXPECT_EQ(l.counts[BR], kMaxPerAnchor);  // unchanged, capacity respected
  EXPECT_EQ(l.counts[BC], 1);              // landed at next search column
}
