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

// The 48 KB framebuffer is split into two slots. wolfSSL holds one large
// receive buffer through the handshake (see the ShrinkInputBuffer patch), then
// asks for a second ~16 KB buffer for the first application record. A single
// slot left that second request on the heap, which on a busy OPDS fetch is a
// 12 KB hole: MEMORY_E at exactly 13553 bytes received. Two slots fit in 48 KB
// with room to spare.
// Set only while a loan is running.
uint8_t* g_block = nullptr;
size_t g_blockLen = 0;

constexpr int NSLOTS = 2;
uint8_t* g_slot[NSLOTS] = {};
size_t g_slotLen[NSLOTS] = {};
bool g_slotUsed[NSLOTS] = {};
// Every address ever handed to wolfSSL, kept FOREVER. wolfSSL can free a
// buffer after the loan has ended, and handing the framebuffer's address to
// the real free() corrupts the heap. Recognising the address for the rest of
// the run costs two pointers and makes that impossible.
uint8_t* g_everServed[NSLOTS] = {};

// Below this, an allocation is a session structure or a bignum temp and belongs
// on the heap; the slots are reserved for the allocations that do not fit
// there. A 16 KB TLS record asks for 16640 bytes.
constexpr size_t MIN_BLOCK_ALLOC = 8192;
// Two records plus wolfSSL headers. The framebuffer is 48 KB; claim() returns
// the whole block if it is at least this long.
constexpr size_t NEEDED = 40 * 1024;

int slotOf(const void* ptr) {
  for (int i = 0; i < NSLOTS; ++i) {
    if (ptr == g_everServed[i] || ptr == g_slot[i]) return i;
  }
  return -1;
}

#if defined(FREEINK_NET_WOLFSSL)
bool g_installed = false;

void* scratchMalloc(size_t size) {
  if (g_block && size >= MIN_BLOCK_ALLOC) {
    for (int i = 0; i < NSLOTS; ++i) {
      if (!g_slotUsed[i] && g_slot[i] && size <= g_slotLen[i]) {
        g_slotUsed[i] = true;
        g_everServed[i] = g_slot[i];
        return g_slot[i];
      }
    }
  }
  return malloc(size);
}

void scratchFree(void* ptr) {
  if (ptr == nullptr) return;
  const int i = slotOf(ptr);
  if (i >= 0) {
    g_slotUsed[i] = false;
    return;
  }
  free(ptr);
}

void* scratchRealloc(void* ptr, size_t size) {
  const int i = slotOf(ptr);
  if (i < 0) return realloc(ptr, size);
  if (ptr == g_slot[i] && size <= g_slotLen[i]) {
    g_slotUsed[i] = true;
    return g_slot[i];
  }
  void* moved = malloc(size);
  if (!moved) return nullptr;
  const size_t copy = g_slotLen[i] < size ? g_slotLen[i] : size;
  memcpy(moved, ptr, copy);
  g_slotUsed[i] = false;
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
  // Installed once and never taken back out: see g_everServed. Swapping the
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
  const size_t slot = len / NSLOTS;
  for (int i = 0; i < NSLOTS; ++i) {
    g_slot[i] = block + (i * slot);
    g_slotLen[i] = (i == NSLOTS - 1) ? (len - i * slot) : slot;
    g_slotUsed[i] = false;
  }
  active_ = true;
  LOG_DBG("TLS", "Lending %u bytes of framebuffer to wolfSSL (%d slots of %u)", static_cast<unsigned>(len), NSLOTS,
          static_cast<unsigned>(slot));
#endif
}

Session::~Session() {
#if defined(FREEINK_NET_WOLFSSL)
  if (!active_) return;
  for (int i = 0; i < NSLOTS; ++i) {
    if (g_slotUsed[i]) {
      LOG_ERR("TLS", "%u bytes still lent to wolfSSL after the transfer", static_cast<unsigned>(g_slotLen[i]));
      g_slotUsed[i] = false;
    }
    g_slot[i] = nullptr;
    g_slotLen[i] = 0;
  }
  buildscratch::release(g_block);
  g_block = nullptr;
  g_blockLen = 0;
  active_ = false;
#endif
}

}  // namespace tls_scratch
