#include <gtest/gtest.h>

#include "sleep/persist/WholeFileReadGate.h"

using crosspoint::persist::wholeFileReadAffordable;

TEST(WholeFileReadGate, SmallFileWithAmpleHeapIsAffordable) {
  EXPECT_TRUE(wholeFileReadAffordable(/*fileSize=*/10 * 1024, /*largestFreeBlock=*/100 * 1024));
}

TEST(WholeFileReadGate, LargeFileOnLowHeapIsRefused) {
  // 20 KB file needs ~60.5 KB contiguous; only 40 KB free -> refuse (bail to
  // the direct-pick fallback rather than abort on a throwing alloc).
  EXPECT_FALSE(wholeFileReadAffordable(/*fileSize=*/20 * 1024, /*largestFreeBlock=*/40 * 1024));
}

TEST(WholeFileReadGate, EmptyFileIsAlwaysAffordable) {
  EXPECT_TRUE(wholeFileReadAffordable(0, 512));
  EXPECT_TRUE(wholeFileReadAffordable(0, 4096));
}

TEST(WholeFileReadGate, BoundaryAtThreeXPlusSlack) {
  // need = fileSize*3 + 512.
  EXPECT_TRUE(wholeFileReadAffordable(1000, 3000 + 512));
  EXPECT_FALSE(wholeFileReadAffordable(1000, 3000 + 511));
}

TEST(WholeFileReadGate, ImplausiblyLargeFileRefusedRegardlessOfHeap) {
  // A > 1 MiB file is never legitimate here; refuse even with a huge free block.
  EXPECT_FALSE(wholeFileReadAffordable((size_t{1} << 20) + 1, size_t{1} << 30));
}
