#include <gtest/gtest.h>

#include "TlsHeapPolicy.h"

using namespace tls_heap;

TEST(TlsHeapPolicy, OnTheHeapAloneTheOldFloorStands) {
  // Without the framebuffer lent, both wolfSSL record buffers come off the heap:
  // the 30000 floor from before this policy is unchanged.
  EXPECT_EQ(MIN_FREE_HEAP, 30000u);
  EXPECT_TRUE(canStartTls(30000, 20000, false));
  EXPECT_FALSE(canStartTls(29999, 20000, false));
}

TEST(TlsHeapPolicy, WithTheFramebufferLentTheFloorDrops) {
  // The record buffers live in the lent framebuffer; the heap only holds the
  // session, so a reader that the old floor refused can go ahead.
  EXPECT_LT(MIN_FREE_WITH_SCRATCH, MIN_FREE_HEAP);
  EXPECT_TRUE(canStartTls(MIN_FREE_WITH_SCRATCH, MIN_BLOCK, true));
  EXPECT_FALSE(canStartTls(MIN_FREE_WITH_SCRATCH - 1, MIN_BLOCK, true));
}

TEST(TlsHeapPolicy, AFragmentedHeapIsRefusedWhateverItsTotal) {
  // wolfSSL's bignum temps are single ~4 KB blocks; plenty of scattered bytes
  // still fail the handshake.
  EXPECT_FALSE(canStartTls(60000, MIN_BLOCK - 1, true));
  EXPECT_FALSE(canStartTls(60000, MIN_BLOCK - 1, false));
  EXPECT_TRUE(canStartTls(60000, MIN_BLOCK, false));
}

TEST(TlsHeapPolicy, TheScratchFloorNeverExceedsTheHeapFloor) {
  // Lending the framebuffer can only make a fetch cheaper, never dearer: a
  // reader that passes on the heap alone must pass with the loan too.
  for (uint32_t free = 0; free < 70000; free += 500) {
    if (canStartTls(free, MIN_BLOCK, false)) EXPECT_TRUE(canStartTls(free, MIN_BLOCK, true)) << free;
  }
}
