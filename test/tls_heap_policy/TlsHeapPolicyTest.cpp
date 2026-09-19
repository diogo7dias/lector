#include <gtest/gtest.h>

#include "TlsHeapPolicy.h"

using namespace tls_heap;

TEST(TlsHeapPolicy, OnTheHeapAloneTheOldFloorStands) {
  EXPECT_EQ(MIN_FREE_HEAP, 30000u);
  EXPECT_EQ(MIN_BLOCK, 8192u);
  EXPECT_TRUE(canStartTls(30000, 8192, false, 0, 0));
  EXPECT_FALSE(canStartTls(29999, 8192, false, 0, 0));
  EXPECT_FALSE(canStartTls(60000, 8191, false, 0, 0));
}

TEST(TlsHeapPolicy, X3ManifestAndFontFileUseTheSameWorkingPool) {
  // /tmp/x3-log3.log: the successful manifest and all three refused file attempts.
  EXPECT_TRUE(canStartTls(26252, 12788, true, 51456, 51456));
  EXPECT_TRUE(canStartTls(18688, 5108, true, 51456, 51456));
  EXPECT_TRUE(canStartTls(18568, 5108, true, 51456, 51456));
  EXPECT_TRUE(canStartTls(18584, 5108, true, 51456, 51456));
  EXPECT_FALSE(canStartTls(18568, 5108, false, 0, 0));
}

TEST(TlsHeapPolicy, AClaimedPoolMustHaveFreeAndContiguousSpace) {
  EXPECT_TRUE(canStartTls(18568, 5108, true, MIN_POOL_FREE, MIN_POOL_BLOCK));
  // Even a healthy system heap cannot excuse a depleted or fragmented pool.
  EXPECT_FALSE(canStartTls(60000, 30000, true, MIN_POOL_FREE - 1, MIN_POOL_BLOCK));
  EXPECT_FALSE(canStartTls(60000, 30000, true, 51456, MIN_POOL_BLOCK - 1));
  EXPECT_FALSE(canStartTls(60000, 30000, true, 0, 0));
}

TEST(TlsHeapPolicy, ScratchCannotExcuseAStarvedSystemHeap) {
  EXPECT_TRUE(canStartTls(MIN_FREE_WITH_SCRATCH, MIN_BLOCK_WITH_SCRATCH, true, 51456, 51456));
  EXPECT_FALSE(canStartTls(MIN_FREE_WITH_SCRATCH - 1, MIN_BLOCK_WITH_SCRATCH, true, 51456, 51456));
  EXPECT_FALSE(canStartTls(18568, MIN_BLOCK_WITH_SCRATCH - 1, true, 51456, 51456));
  EXPECT_FALSE(canStartTls(1004, 1004, true, 51456, 51456));
}
