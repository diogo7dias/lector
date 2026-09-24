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

TEST(TlsHeapPolicy, SuccessLogStillAdmitsManifestAndEveryFontAfterReclaimingRows) {
  EXPECT_TRUE(canStartTls(26460, 13300, true, 51456, 51188));
  // C3 ABI: 33 rows * (52-byte ListItem + two 24-byte strings). Do not
  // credit freed string contents or allocator overhead. All log4 file gates.
  constexpr uint32_t reclaimedRows = 33 * (52 + 2 * 24);
  for (const uint32_t freeHeap : {18704u, 18752u, 18784u}) {
    // Every admission the device actually completed must still be admitted,
    // with or without the row reclamation. The floor no longer leans on that
    // reclamation to let real work through: it only widens the margin.
    EXPECT_TRUE(canStartTls(freeHeap, 5108, true, 51456, 51188, Transfer::FontFile));
    EXPECT_TRUE(canStartTls(freeHeap + reclaimedRows, 5108, true, 51456, 51188, Transfer::FontFile));
  }
  // x3-log5 general gate: both refused admissions must now pass.
  EXPECT_TRUE(canStartTls(24528, 8692, true, 51456, 51188));
  EXPECT_TRUE(canStartTls(24652, 12788, true, 51456, 51188));
}

TEST(TlsHeapPolicy, FloorsBudgetMeasuredDemandAndAdmitEveryObservedTransfer) {
  constexpr uint32_t removedRxBuffer = 1436;
  constexpr uint32_t generalDemand = 26460u - 3556u - removedRxBuffer;  // 21468
  constexpr uint32_t fontDemand = 18752u - 1896u - removedRxBuffer;     // 15420
  // A floor below measured demand runs the transfer out of heap mid-handshake.
  EXPECT_GT(minFree(true), generalDemand);
  EXPECT_GT(minFree(true, Transfer::FontFile), fontDemand);
  // x3-log5: free at the general gate was 24528, then 24652, on a device where
  // the same fetch had succeeded from 26460. A floor above the LOWEST observed
  // admission refuses work that fits, which is what 25600 did. This bound and
  // the one above squeeze the general floor into 21468 < floor <= 24528; the
  // earlier "4096 margin" rule asked for 25564 and could not hold with it.
  EXPECT_LE(minFree(true), 24528u);
  EXPECT_LE(minFree(true, Transfer::FontFile), 18704u);
  EXPECT_EQ(minFree(false, Transfer::FontFile), MIN_FREE_HEAP);
  EXPECT_FALSE(canStartTls(MIN_FREE_FONT_FILE_WITH_SCRATCH - 1, 5108, true, 51456, 51188, Transfer::FontFile));
  EXPECT_TRUE(canStartTls(MIN_FREE_FONT_FILE_WITH_SCRATCH, 5108, true, 51456, 51188, Transfer::FontFile));
}

TEST(TlsHeapPolicy, AClaimedPoolMustHaveFreeAndContiguousSpace) {
  EXPECT_TRUE(canStartTls(MIN_FREE_WITH_SCRATCH, 5108, true, MIN_POOL_FREE, MIN_POOL_BLOCK));
  // Even a healthy system heap cannot excuse a depleted or fragmented pool.
  EXPECT_FALSE(canStartTls(60000, 30000, true, MIN_POOL_FREE - 1, MIN_POOL_BLOCK));
  EXPECT_FALSE(canStartTls(60000, 30000, true, 51456, MIN_POOL_BLOCK - 1));
  EXPECT_FALSE(canStartTls(60000, 30000, true, 0, 0));
}

TEST(TlsHeapPolicy, ScratchCannotExcuseAStarvedSystemHeap) {
  EXPECT_TRUE(canStartTls(MIN_FREE_WITH_SCRATCH, MIN_BLOCK_WITH_SCRATCH, true, 51456, 51456));
  EXPECT_FALSE(canStartTls(MIN_FREE_WITH_SCRATCH - 1, MIN_BLOCK_WITH_SCRATCH, true, 51456, 51456));
  EXPECT_FALSE(canStartTls(MIN_FREE_WITH_SCRATCH, MIN_BLOCK_WITH_SCRATCH - 1, true, 51456, 51456));
  EXPECT_FALSE(canStartTls(1004, 1004, true, 51456, 51456));
}
