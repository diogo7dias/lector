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

// x3-log4-SUCCESS.log (2026-09-19), scoped INTERNAL heap minima:
// manifest: 26460 -> 3556 = 22904 bytes; worst font: 18752 -> 1896 = 16856.
// SecureClient now receives directly into wolfSSL's scratch-backed record:
// NetworkClient's separate 1436-byte malloc/copy is gone. Credit only its
// payload, not allocator overhead or any improvement in packet draining.
// Projected demand: general 22904 - 1436 = 21468; font 16856 - 1436 = 15420.
// Floors leave 25600 - 21468 = 4132 and 20480 - 15420 = 5060 bytes respectively.
// FontDownload also releases its unused list capacities before the file gate;
// without that reclamation the old 18752-byte admission WOULD be refused.
// Pool floors stay 40960/16640: receive uses the existing wolfSSL allocation.
// ponytail: these are measured-demand projections, not post-fix measurements
// or bounds on other networks. Keep scoped minima; validate >=4096 on X3,
// all nine CRC-verified downloads and zero spills before release. IDF sums
// per-region minima, which need not occur simultaneously.
enum class Transfer { General, FontFile };
constexpr uint32_t MIN_FREE_WITH_SCRATCH = 25 * 1024;
constexpr uint32_t MIN_FREE_FONT_FILE_WITH_SCRATCH = 20 * 1024;
constexpr uint32_t MIN_BLOCK_WITH_SCRATCH = 4096;

// Without scratch, preserve the existing heap-only protection.
constexpr uint32_t MIN_BLOCK = 8192;
constexpr uint32_t minFree(const bool scratchActive, const Transfer transfer = Transfer::General) {
  if (!scratchActive) return MIN_FREE_HEAP;
  return transfer == Transfer::FontFile ? MIN_FREE_FONT_FILE_WITH_SCRATCH : MIN_FREE_WITH_SCRATCH;
}
constexpr uint32_t minBlock(const bool scratchActive) { return scratchActive ? MIN_BLOCK_WITH_SCRATCH : MIN_BLOCK; }

constexpr bool canStartTls(const uint32_t freeHeap, const uint32_t largestBlock, const bool scratchActive,
                           const uint32_t poolFree, const uint32_t poolLargestBlock,
                           const Transfer transfer = Transfer::General) {
  return freeHeap >= minFree(scratchActive, transfer) && largestBlock >= minBlock(scratchActive) &&
         (!scratchActive || (poolFree >= MIN_POOL_FREE && poolLargestBlock >= MIN_POOL_BLOCK));
}

}  // namespace tls_heap
