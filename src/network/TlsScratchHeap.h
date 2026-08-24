#pragma once

#include <cstddef>

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
 * say until the file lands. This routes wolfSSL's large allocations into that
 * block, so the record buffer costs the heap nothing.
 */
namespace tls_scratch {

/**
 * Installs the wolfSSL allocators for as long as it lives, and claims the lent
 * framebuffer block. Construct INSIDE a GfxRenderer::FrameBufferLoan and after
 * the screen the panel should hold has been displayed; without an active loan
 * there is nothing to claim and every allocation falls through to the heap,
 * which is exactly the old behaviour.
 *
 * One at a time, and not while another task is using wolfSSL: the allocators
 * are global. The font download is the only caller, and it blocks its activity
 * for the whole transfer.
 */
class Session {
 public:
  Session();
  ~Session();
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  /** True when the framebuffer block was claimed, so large allocations avoid the heap. */
  bool active() const { return active_; }

 private:
  bool active_ = false;
};

}  // namespace tls_scratch
