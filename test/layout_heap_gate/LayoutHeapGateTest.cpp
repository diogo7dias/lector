#include <gtest/gtest.h>

#include "Epub/parsers/LayoutHeapGate.h"

using crosspoint::layoutHeapCritical;

TEST(LayoutHeapGate, AmpleHeapIsNotCritical) {
  EXPECT_FALSE(layoutHeapCritical(/*freeHeap=*/64 * 1024, /*largestFreeBlock=*/48 * 1024));
}

TEST(LayoutHeapGate, LowTotalFreeHeapIsCritical) {
  // Plenty of contiguous room but the total free heap is under the floor.
  EXPECT_TRUE(layoutHeapCritical(/*freeHeap=*/12 * 1024, /*largestFreeBlock=*/12 * 1024));
}

TEST(LayoutHeapGate, FragmentedHeapIsCritical) {
  // Lots of total free heap, but the largest contiguous block is too small to
  // satisfy a medium allocation — this is the real crash mode on the device.
  EXPECT_TRUE(layoutHeapCritical(/*freeHeap=*/64 * 1024, /*largestFreeBlock=*/4 * 1024));
}

TEST(LayoutHeapGate, FreeHeapBoundaryIsExclusive) {
  // Strictly below the free-heap floor is critical; exactly at the floor (with
  // ample contiguous room) is not.
  EXPECT_TRUE(layoutHeapCritical(24 * 1024 - 1, 32 * 1024));
  EXPECT_FALSE(layoutHeapCritical(24 * 1024, 32 * 1024));
}

TEST(LayoutHeapGate, LargestBlockBoundaryIsExclusive) {
  EXPECT_TRUE(layoutHeapCritical(64 * 1024, 16 * 1024 - 1));
  EXPECT_FALSE(layoutHeapCritical(64 * 1024, 16 * 1024));
}

TEST(LayoutHeapGate, ZeroHeapIsCritical) { EXPECT_TRUE(layoutHeapCritical(0, 0)); }
