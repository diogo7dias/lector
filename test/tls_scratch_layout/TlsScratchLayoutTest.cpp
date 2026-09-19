#include <gtest/gtest.h>

#include "TlsScratchLayout.h"

using namespace tls_scratch;

namespace {
// The two framebuffers this firmware lends: the X4's 800x480 panel and the
// X3's 792x528 one.
constexpr size_t X4_FRAMEBUFFER = 800 * 480 / 8;   // 48000
constexpr size_t X3_FRAMEBUFFER = 792 * 528 / 8;   // 52272
// What wolfSSL asks for when a peer ignores max_fragment_length and sends a
// 16 KB record (TlsScratchHeap.h).
constexpr size_t RECORD_BUFFER = 16640;
}  // namespace

TEST(TlsScratchLayout, BothPanelsClearTheClaimThreshold) {
  // claim() refuses a block under NEEDED, and a refused claim means every
  // wolfSSL allocation falls back to the heap -- the failure this layout exists
  // to prevent.
  EXPECT_GE(X4_FRAMEBUFFER, NEEDED);
  EXPECT_GE(X3_FRAMEBUFFER, NEEDED);
}

TEST(TlsScratchLayout, ASlotHoldsA16KbRecordBuffer) {
  EXPECT_GE(SLOT_BYTES, RECORD_BUFFER);
  // Two can be live at once: the handshake buffer and the first application
  // record. Both must fit in the smallest framebuffer.
  EXPECT_GE(X4_FRAMEBUFFER, static_cast<size_t>(NSLOTS) * SLOT_BYTES);
}

TEST(TlsScratchLayout, ARecordBufferAlwaysTakesASlotRatherThanTheTail) {
  // The tail is for the small allocations. A record buffer must never be able
  // to land there and leave the next record with no slot to take.
  EXPECT_GE(RECORD_BUFFER, MIN_BLOCK_ALLOC);
}

TEST(TlsScratchLayout, EveryPanelLeavesATailWorthHaving) {
  // The point of the tail: wolfSSL's session, decoded certificate and bignum
  // temporaries stop competing for the system heap. On the X3 this is the heap
  // that had 26124 bytes free in 12788-byte pieces when the fetch started and
  // 9700 when the read died -- a 16424-byte drawdown, of which everything
  // wolfSSL asked for belongs here rather than there.
  EXPECT_EQ(tailBytes(X4_FRAMEBUFFER), X4_FRAMEBUFFER - tailOffset());
  EXPECT_EQ(tailBytes(X3_FRAMEBUFFER), X3_FRAMEBUFFER - tailOffset());
  // Not the whole 16424 -- part of that drawdown is lwIP's receive window and
  // the socket, which wolfSSL's allocators never see. 12288 is the floor that
  // keeps the slots from eating the panel with the smaller framebuffer: two
  // 18432-byte slots left the X4 with 11136 and failed it.
  EXPECT_GE(tailBytes(X4_FRAMEBUFFER), 12288u);
  EXPECT_GE(tailBytes(X3_FRAMEBUFFER), 12288u);
}

TEST(TlsScratchLayout, ABlockTooSmallForBothSlotsAsksForNoTail) {
  // Not reachable from either panel, but the arithmetic must not wrap: an
  // underflow here would register a ~4 GB heap over the framebuffer.
  EXPECT_EQ(tailBytes(0), 0u);
  EXPECT_EQ(tailBytes(tailOffset()), 0u);
  EXPECT_EQ(tailBytes(tailOffset() - 1), 0u);
}
