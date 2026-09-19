#include <gtest/gtest.h>

#include "TlsScratchLayout.h"

using namespace tls_scratch;

namespace {
// The two framebuffers this firmware lends: the X4's 800x480 panel and the
// X3's 792x528 one.
constexpr size_t X4_FRAMEBUFFER = 800 * 480 / 8;  // 48000
constexpr size_t X3_FRAMEBUFFER = 792 * 528 / 8;  // 52272

// What the fixed-slot layout this replaced left for everything that is not a
// record buffer: 52272 - 2 * 17408 on the X3, 48000 - 2 * 17408 on the X4.
constexpr size_t OLD_X3_TAIL = 17456;
constexpr size_t OLD_X4_TAIL = 13184;
}  // namespace

TEST(TlsScratchLayout, BothPanelsClearTheClaimThreshold) {
  // claim() refuses a block under NEEDED, and a refused claim means every
  // wolfSSL allocation falls back to the heap -- the failure this layout exists
  // to prevent.
  EXPECT_GE(X4_FRAMEBUFFER, NEEDED);
  EXPECT_GE(X3_FRAMEBUFFER, NEEDED);
}

TEST(TlsScratchLayout, EveryPanelHoldsBothRecordBuffersAtOnce) {
  // GrowInputBuffer fills the larger buffer before freeing the smaller one, so
  // the block has to carry two of them for the moment in between.
  EXPECT_GE(X4_FRAMEBUFFER, static_cast<size_t>(MAX_LIVE_RECORDS) * RECORD_BYTES);
  EXPECT_GE(X3_FRAMEBUFFER, static_cast<size_t>(MAX_LIVE_RECORDS) * RECORD_BYTES);
  EXPECT_GE(NEEDED, static_cast<size_t>(MAX_LIVE_RECORDS) * RECORD_BYTES);
}

TEST(TlsScratchLayout, OnePoolBeatsTheFixedSlotsEvenAtTheWorstMoment) {
  // The X3 log of 2026-09-19 measured the fixed tail running to 32 free bytes
  // with 33 allocations spilling onto the system heap. The worst moment under
  // one pool -- both record buffers live -- must still be no tighter than that
  // permanent figure was, or this trades one starvation for another.
  EXPECT_GT(smallPoolBytes(X3_FRAMEBUFFER), OLD_X3_TAIL);
  EXPECT_GT(smallPoolBytes(X4_FRAMEBUFFER), OLD_X4_TAIL);
  EXPECT_EQ(smallPoolBytes(X3_FRAMEBUFFER), 18992u);
  EXPECT_EQ(smallPoolBytes(X4_FRAMEBUFFER), 14720u);
}

TEST(TlsScratchLayout, WithOneRecordLiveTheSmallAllocationsGetRoughlyDouble) {
  // The steady state of a session: one record buffer, everything else sharing
  // what is left. This is the number that has to cover the 17424 bytes the old
  // tail was measured using plus the 33 allocations it could not.
  EXPECT_EQ(X3_FRAMEBUFFER - RECORD_BYTES, 35632u);
  EXPECT_EQ(X4_FRAMEBUFFER - RECORD_BYTES, 31360u);
  EXPECT_GT(X3_FRAMEBUFFER - RECORD_BYTES, 2 * OLD_X3_TAIL);
  EXPECT_GT(X4_FRAMEBUFFER - RECORD_BYTES, 2 * OLD_X4_TAIL);
}

TEST(TlsScratchLayout, ABlockTooSmallForBothRecordsAsksForNoPool) {
  // Not reachable from either panel, but the arithmetic must not wrap: an
  // underflow here would report a ~4 GB pool over the framebuffer.
  constexpr size_t records = static_cast<size_t>(MAX_LIVE_RECORDS) * RECORD_BYTES;
  EXPECT_EQ(smallPoolBytes(0), 0u);
  EXPECT_EQ(smallPoolBytes(records), 0u);
  EXPECT_EQ(smallPoolBytes(records - 1), 0u);
}
