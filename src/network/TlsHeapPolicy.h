#pragma once

#include <cstdint>

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

// Floor while the framebuffer is lent to wolfSSL (tls_scratch::Session active):
// the two large record buffers are carved from the lent 48 KB and the heap only
// carries the session, the HTTP client and the caller's parser. Measured on an
// X4 (C3) OPDS fetch with one slot lent: from 38716 bytes free the transfer
// reached 18116 before asking for its second record buffer, i.e. 20600 bytes
// of heap for everything but the two large buffers. 24000 keeps 3 KB over that
// on a fetch whose parser (ReleaseJsonParser) is smaller than OPDS's.
constexpr uint32_t MIN_FREE_WITH_SCRATCH = 24000;

// Largest contiguous block either way: wolfSSL's SP math temps are ~4 KB each
// (SecureClient.cpp) and two can be live at once during the handshake.
constexpr uint32_t MIN_BLOCK = 8192;

constexpr uint32_t minFree(const bool scratchActive) { return scratchActive ? MIN_FREE_WITH_SCRATCH : MIN_FREE_HEAP; }

constexpr bool canStartTls(const uint32_t freeHeap, const uint32_t largestBlock, const bool scratchActive) {
  return freeHeap >= minFree(scratchActive) && largestBlock >= MIN_BLOCK;
}

}  // namespace tls_heap
