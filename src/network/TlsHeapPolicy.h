#pragma once

#include <cstdint>

#include "TlsScratchLayout.h"

// Whether a TLS session may be started on the heap the reader has right now.
// Kept free of ESP headers so the numbers can be reasoned about (and tested)
// on the host; the callers feed ESP.getFreeHeap() / ESP.getMaxAllocHeap().
//
// A handshake started on a nearly empty heap does not fail cleanly: wolfSSL
// spent 60 seconds inside its retry with 1004 bytes free and the reader was
// unresponsive until the watchdog reset it (FontDownloadActivity.h). Refusing
// up front trades that hang for a message.
namespace tls_heap {

// Floor when both wolfSSL record buffers come off the heap. Unchanged from the
// gate that preceded this policy: a font download with 43000 bytes free died
// with MEMORY_E at 11532 asking for its 16640-byte record buffer
// (TlsScratchHeap.h), so a fetch on the heap alone wants nearly 48 KB and
// 30000 is already the least that avoids the hang, not a comfortable margin.
constexpr uint32_t MIN_FREE_HEAP = 30000;

// X3 log3 (2026-09-19): 51456 usable -> 18196 low-water = 33260 bytes
// served by scratch, with ZERO wolfSSL spills. Require the existing 40 KiB
// scratch budget as FREE bytes (7700 over that measured peak), and room for
// one contiguous 16640-byte record. A claimed but depleted pool is not enough.
constexpr uint32_t MIN_POOL_FREE = tls_scratch::NEEDED;
constexpr uint32_t MIN_POOL_BLOCK = tls_scratch::RECORD_BYTES;

// Non-wolfSSL heap: 26252 at the manifest gate -> 22360 at the redirect,
// i.e. 3892 retained bytes. Round up to 4096 and budget two such chunks:
// one for the observed HTTP/URL state, one for live transport/header churn.
// This is a budget from a snapshot, NOT a measured peak; log3's
// boot minimum includes the later manifest parse and cannot isolate TLS.
// ponytail: 8192/4096 is specific to this streaming GitHub flow; use the
// Session's scoped system-heap low-water to recalibrate for other workloads.
constexpr uint32_t MIN_FREE_WITH_SCRATCH = 2 * 4096;
constexpr uint32_t MIN_BLOCK_WITH_SCRATCH = 4096;

// Without scratch, preserve the existing heap-only protection.
constexpr uint32_t MIN_BLOCK = 8192;
constexpr uint32_t minFree(const bool scratchActive) { return scratchActive ? MIN_FREE_WITH_SCRATCH : MIN_FREE_HEAP; }
constexpr uint32_t minBlock(const bool scratchActive) { return scratchActive ? MIN_BLOCK_WITH_SCRATCH : MIN_BLOCK; }

constexpr bool canStartTls(const uint32_t freeHeap, const uint32_t largestBlock, const bool scratchActive,
                           const uint32_t poolFree, const uint32_t poolLargestBlock) {
  return freeHeap >= minFree(scratchActive) && largestBlock >= minBlock(scratchActive) &&
         (!scratchActive || (poolFree >= MIN_POOL_FREE && poolLargestBlock >= MIN_POOL_BLOCK));
}

}  // namespace tls_heap
