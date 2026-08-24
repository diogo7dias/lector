#include "TlsScratchHeap.h"

#include <Logging.h>

#include <cstdlib>
#include <cstring>

#include "BuildScratch.h"

#if defined(FREEINK_NET_WOLFSSL)
#include <wolfssl/wolfcrypt/settings.h>
// settings.h first: it defines the build's feature macros, and memory.h picks
// its callback signatures from them.
#include <wolfssl/wolfcrypt/memory.h>
#endif

namespace tls_scratch {
namespace {

// The block serves one allocation at a time, which is all that is asked of it:
// wolfSSL keeps a single receive buffer for a session (see the ShrinkInputBuffer
// patch in scripts/patch_wolfssl.py, which stops it being freed and reallocated
// per record). A second large request while the first is out falls through to
// the heap and behaves as it always did.
// Set only while a loan is running: what new large allocations may be served from.
uint8_t* g_block = nullptr;
size_t g_blockLen = 0;
size_t g_inUse = 0;
// The last block ever served, kept FOREVER. wolfSSL can free a buffer after the
// loan has ended (a session torn down later, a context outliving the transfer),
// and handing the framebuffer's address to the real free() corrupts the heap and
// takes the reader down with no recorded reason. Recognising the address for the
// rest of the run costs one pointer and makes that impossible.
uint8_t* g_servedBlock = nullptr;

// Below this, an allocation is a session structure or a bignum temp and belongs
// on the heap; the block is reserved for the one allocation that does not fit
// there. A 16 KB TLS record asks for 16640 bytes.
constexpr size_t MIN_BLOCK_ALLOC = 8192;
// Enough for a 16 KB record plus wolfSSL's headers and padding, with the rest of
// the framebuffer unused rather than handed to a second claimant.
constexpr size_t NEEDED = 20 * 1024;

#if defined(FREEINK_NET_WOLFSSL)
bool g_installed = false;

void* scratchMalloc(size_t size) {
  if (g_block && g_inUse == 0 && size >= MIN_BLOCK_ALLOC && size <= g_blockLen) {
    g_inUse = size;
    g_servedBlock = g_block;
    return g_block;
  }
  return malloc(size);
}

void scratchFree(void* ptr) {
  if (ptr == nullptr) return;
  if (ptr == g_servedBlock) {
    g_inUse = 0;
    return;
  }
  free(ptr);
}

void* scratchRealloc(void* ptr, size_t size) {
  if (ptr != g_servedBlock) return realloc(ptr, size);
  // Growing within the block costs nothing while it is still lent; outgrowing it,
  // or growing after the loan ended, means copying what is there onto the heap and
  // handing the block back.
  if (ptr == g_block && size <= g_blockLen) {
    g_inUse = size;
    return g_block;
  }
  void* moved = malloc(size);
  if (!moved) return nullptr;
  memcpy(moved, ptr, g_inUse < size ? g_inUse : size);
  g_inUse = 0;
  return moved;
}
#endif

}  // namespace

Session::Session() {
#if defined(FREEINK_NET_WOLFSSL)
  size_t len = 0;
  uint8_t* block = buildscratch::claim(NEEDED, &len);
  if (!block) {
    LOG_DBG("TLS", "No build scratch to lend; wolfSSL stays on the heap");
    return;
  }
  // Installed once and never taken back out: see g_servedBlock. Swapping the
  // allocators back would leave wolfSSL's real free() holding an address that
  // belongs to the framebuffer.
  if (!g_installed) {
    if (wolfSSL_SetAllocators(scratchMalloc, scratchFree, scratchRealloc) != 0) {
      LOG_ERR("TLS", "Failed to install the scratch allocators");
      buildscratch::release(block);
      return;
    }
    g_installed = true;
  }
  g_block = block;
  g_blockLen = len;
  active_ = true;
  LOG_DBG("TLS", "Lending %u bytes of framebuffer to wolfSSL", static_cast<unsigned>(len));
#endif
}

Session::~Session() {
#if defined(FREEINK_NET_WOLFSSL)
  if (!active_) return;
  if (g_inUse != 0) {
    // The session outlived the transfer and still points into the framebuffer.
    // Drawing over those bytes is harmless (nothing reads them again) and the
    // eventual free is caught by g_servedBlock, so the block goes back either
    // way. Worth saying out loud, because it means an assumption slipped.
    LOG_ERR("TLS", "%u bytes still lent to wolfSSL after the transfer", static_cast<unsigned>(g_inUse));
    g_inUse = 0;
  }
  buildscratch::release(g_block);
  g_block = nullptr;
  g_blockLen = 0;
  active_ = false;
#endif
}

}  // namespace tls_scratch
