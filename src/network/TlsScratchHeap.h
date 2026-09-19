#pragma once

#include <cstddef>
#include <cstdint>

/**
 * Lends the framebuffer's bytes to wolfSSL for the length of a transfer.
 *
 * A TLS peer that ignores our max_fragment_length request sends 16 KB records,
 * and wolfSSL sizes its receive buffer to the record: one ~16.6 KB contiguous
 * allocation, held for the session. With WiFi up, a font download starts with
 * about 43000 bytes free, and the session plus that buffer wants more than that
 * -- the transfer died with MEMORY_E (-125) at 11532 bytes free, every time,
 * a few kilobytes short.
 *
 * The reader already has the memory: the 48 KB framebuffer, which
 * GfxRenderer::FrameBufferLoan lends out in place for heap-hungry phases.
 * Nothing may draw while it is lent, which suits a transfer that has nothing to
 * say until the file lands. The whole block is registered as a heap of its own
 * and every wolfSSL allocation -- record buffers, session, decoded certificate,
 * bignum temporaries -- is served from it, so none of them costs the system
 * heap anything. See TlsScratchLayout.h for why it is one pool and not slots.
 */
namespace tls_scratch {

/** True while a Session holds the lent framebuffer, so a heap gate can use the
 * lower floor (tls_heap::MIN_FREE_WITH_SCRATCH): the large record buffers are
 * not going to come off the heap. */
bool isActive();

/**
 * How many allocations the current (or last) Session could not serve from the
 * lent framebuffer and had to leave on the system heap, how many bytes those
 * came to, and the largest of them. Zero means every byte wolfSSL asked for
 * came out of the loan, so a handshake that still failed did not fail for want
 * of room in the block.
 *
 * The byte total is what sizes the block for the next build: a count alone says
 * the pool was too small without saying by how much. Reset by each Session.
 */
uint32_t heapFallbackCount();
uint32_t heapFallbackBytes();
uint32_t heapFallbackLargest();

/**
 * Free bytes in the lent block right now, and the least it ever held during
 * this Session. Read between the hops of a redirect to see whether a finished
 * hop actually gave its memory back before the next handshake asked for it.
 * Both read 0 once the Session has ended.
 */
size_t poolFreeBytes();
size_t poolLowWaterBytes();

/**
 * The wolfSSL allocation that failed, if one did: how many failed, what the
 * last of them asked for, and how many bytes the system heap had free at that
 * instant.
 *
 * A null returned to wolfSSL is what becomes MEMORY_E (-125), and MEMORY_E
 * reaches the reader through HttpDownloader::NO_CONNECTION -- the same code a
 * wrong access point produces. These counters are how the two are told apart.
 * wolfSSL does not always relabel it as MEMORY_E: an allocation that fails
 * under certificate processing surfaces as PEER_KEY_ERROR (-342) or
 * MP_EXPTMOD_E (-112), so the counters are the only honest evidence.
 *
 * The free-heap reading has to be taken here and not after the transfer: by
 * the time downloadToFile() returns, the session has been freed and the heap
 * reads tens of kilobytes higher than it did at the failure (24244 against
 * 9700 on the X3 log this was written for). Reset by each Session.
 */
uint32_t oomCount();
uint32_t oomSize();
uint32_t oomFreeHeap();

/**
 * Installs the wolfSSL allocators for as long as it lives, and claims the lent
 * framebuffer block. Construct INSIDE a GfxRenderer::FrameBufferLoan and after
 * the screen the panel should hold has been displayed; without an active loan
 * there is nothing to claim and every allocation falls through to the heap,
 * which is exactly the old behaviour.
 *
 * Not while another task is using wolfSSL: the allocators are global. Callers
 * block their activity for the whole transfer.
 */
class Session {
 public:
  Session();
  ~Session();
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  /** True when the framebuffer block was claimed, so allocations avoid the heap. */
  bool active() const { return active_; }

 private:
  bool active_ = false;
};

}  // namespace tls_scratch
