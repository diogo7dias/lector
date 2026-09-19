#include "TlsScratchHeap.h"

#include <Logging.h>
#include <esp_system.h>
#include <multi_heap.h>

#include <cstdlib>
#include <cstring>

#include "BuildScratch.h"
#include "TlsScratchLayout.h"

#if defined(FREEINK_NET_WOLFSSL)
#include <wolfssl/wolfcrypt/settings.h>
// settings.h first: it defines the build's feature macros, and memory.h picks
// its callback signatures from them.
#include <wolfssl/ssl.h>
#include <wolfssl/wolfcrypt/memory.h>
#endif

namespace tls_scratch {
namespace {

// The lent framebuffer, split per TlsScratchLayout.h: two fixed slots for the
// large record buffers, then a tail registered as a heap of its own for
// everything else wolfSSL asks for.
// Set only while a loan is running.
uint8_t* g_block = nullptr;
size_t g_blockLen = 0;

uint8_t* g_slot[NSLOTS] = {};
size_t g_slotLen[NSLOTS] = {};
bool g_slotUsed[NSLOTS] = {};
// Every address ever handed to wolfSSL, kept FOREVER. wolfSSL can free a
// buffer after the loan has ended, and handing the framebuffer's address to
// the real free() corrupts the heap. Recognising the address for the rest of
// the run costs two pointers and makes that impossible.
uint8_t* g_everServed[NSLOTS] = {};

multi_heap_handle_t g_tail = nullptr;
// The tail's address range, likewise kept FOREVER and for the same reason: a
// free() that arrives after the loan ended still has to be recognised as ours
// and dropped rather than passed to the system allocator.
uint8_t* g_tailBase = nullptr;
size_t g_tailLen = 0;

// Diagnostics for the failure screen: how many allocations the loan could not
// serve and had to leave on the system heap, and the largest of them. Zero
// means every byte wolfSSL asked for came out of the framebuffer.
uint32_t g_fallbackCount = 0;
uint32_t g_fallbackLargest = 0;

// The allocation wolfSSL did not get. See oomCount() in the header: this is
// what separates an out-of-memory from an unreachable server, and the free
// heap has to be read here because nothing later still sees this moment.
uint32_t g_oomCount = 0;
uint32_t g_oomSize = 0;
uint32_t g_oomFreeHeap = 0;

int slotOf(const void* ptr) {
  // A null pointer belongs to no slot. Without this, realloc(NULL, n) -- which C
  // code writes for a plain allocation -- matched whichever slot had not been
  // served yet (both sides null) and took the copy path instead of the slots.
  if (ptr == nullptr) return -1;
  for (int i = 0; i < NSLOTS; ++i) {
    if (ptr == g_everServed[i] || ptr == g_slot[i]) return i;
  }
  return -1;
}

bool inTail(const void* ptr) {
  if (ptr == nullptr || g_tailBase == nullptr) return false;
  const auto* p = static_cast<const uint8_t*>(ptr);
  return p >= g_tailBase && p < g_tailBase + g_tailLen;
}

#if defined(FREEINK_NET_WOLFSSL)
bool g_installed = false;

void noteFallback(const size_t size) {
  g_fallbackCount++;
  if (size > g_fallbackLargest) g_fallbackLargest = static_cast<uint32_t>(size);
}

// The last resort of every path below. Runs in the task that called wolfSSL,
// never in an ISR, so reading the heap here is safe -- and it is the only
// place that still sees the heap as it was when the allocation failed.
void* heapFallback(const size_t size) {
  void* p = malloc(size);
  if (!p) {
    g_oomCount++;
    g_oomSize = static_cast<uint32_t>(size);
    g_oomFreeHeap = static_cast<uint32_t>(esp_get_free_heap_size());
  }
  return p;
}

void* scratchMalloc(size_t size) {
  if (g_block) {
    if (size >= MIN_BLOCK_ALLOC) {
      for (int i = 0; i < NSLOTS; ++i) {
        if (!g_slotUsed[i] && g_slot[i] && size <= g_slotLen[i]) {
          g_slotUsed[i] = true;
          g_everServed[i] = g_slot[i];
          return g_slot[i];
        }
      }
      // No slot free, or the ask is larger than one. The tail may still hold a
      // run this long, so try it before falling back to the system heap.
    }
    if (g_tail) {
      if (void* p = multi_heap_malloc(g_tail, size)) return p;
    }
    noteFallback(size);
  }
  return heapFallback(size);
}

void scratchFree(void* ptr) {
  if (ptr == nullptr) return;
  const int i = slotOf(ptr);
  if (i >= 0) {
    g_slotUsed[i] = false;
    return;
  }
  if (inTail(ptr)) {
    // With the loan over, the bytes are the framebuffer's again and the tail
    // heap's bookkeeping is gone: there is nothing to give back and nothing
    // leaks, because the next Session registers the whole tail afresh.
    if (g_tail) multi_heap_free(g_tail, ptr);
    return;
  }
  free(ptr);
}

void* scratchRealloc(void* ptr, size_t size) {
  const int i = slotOf(ptr);
  if (i >= 0) {
    if (ptr == g_slot[i] && size <= g_slotLen[i]) {
      g_slotUsed[i] = true;
      return g_slot[i];
    }
    void* moved = scratchMalloc(size);
    if (!moved) return nullptr;
    const size_t copy = g_slotLen[i] < size ? g_slotLen[i] : size;
    memcpy(moved, ptr, copy);
    g_slotUsed[i] = false;
    return moved;
  }
  if (inTail(ptr)) {
    if (g_tail) {
      if (void* grown = multi_heap_realloc(g_tail, ptr, size)) return grown;
    }
    // The tail cannot hold it any more (or the loan has ended and its
    // bookkeeping with it): move the block out to the system heap. Never copy
    // past the end of the tail, which bounds what the old allocation can be.
    void* moved = heapFallback(size);
    if (!moved) return nullptr;
    const size_t available = static_cast<size_t>(g_tailBase + g_tailLen - static_cast<uint8_t*>(ptr));
    const size_t copy = available < size ? available : size;
    memcpy(moved, ptr, copy);
    if (g_tail) multi_heap_free(g_tail, ptr);
    noteFallback(size);
    return moved;
  }
  void* grown = realloc(ptr, size);
  if (!grown && size > 0) {
    g_oomCount++;
    g_oomSize = static_cast<uint32_t>(size);
    g_oomFreeHeap = static_cast<uint32_t>(esp_get_free_heap_size());
  }
  return grown;
}
#endif

}  // namespace

bool isActive() { return g_block != nullptr; }

uint32_t heapFallbackCount() { return g_fallbackCount; }
uint32_t heapFallbackLargest() { return g_fallbackLargest; }

uint32_t oomCount() { return g_oomCount; }
uint32_t oomSize() { return g_oomSize; }
uint32_t oomFreeHeap() { return g_oomFreeHeap; }

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
  // Before the block is live, so whatever wolfSSL's one-time global setup
  // allocates comes off the system heap and can outlive any loan. Everything
  // below this line may land in the framebuffer, which the panel takes back
  // the moment the transfer ends. Refcounted inside wolfSSL; calling it on
  // every Session is free after the first.
  wolfSSL_Init();

  g_fallbackCount = 0;
  g_fallbackLargest = 0;
  g_oomCount = 0;
  g_oomSize = 0;
  g_oomFreeHeap = 0;
  g_block = block;
  g_blockLen = len;
  for (int i = 0; i < NSLOTS; ++i) {
    g_slot[i] = block + (static_cast<size_t>(i) * SLOT_BYTES);
    g_slotLen[i] = SLOT_BYTES;
    g_slotUsed[i] = false;
  }
  const size_t tail = tailBytes(len);
  if (tail > 0) {
    g_tailBase = block + tailOffset();
    g_tailLen = tail;
    g_tail = multi_heap_register(g_tailBase, tail);
    if (!g_tail) LOG_ERR("TLS", "Could not register the %u-byte scratch tail", static_cast<unsigned>(tail));
  }
  active_ = true;
  LOG_DBG("TLS", "Lending %u bytes of framebuffer to wolfSSL (%d slots of %u, %u-byte tail)",
          static_cast<unsigned>(len), NSLOTS, static_cast<unsigned>(SLOT_BYTES), static_cast<unsigned>(tail));
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
  if (g_tail) {
    // Anything still held here is about to be drawn over. It cannot be rescued
    // -- wolfSSL owns the pointer -- but a silent corruption is worse than a
    // named one, and the count says which allocation to go and look for.
    multi_heap_info_t info = {};
    multi_heap_get_info(g_tail, &info);
    if (info.allocated_blocks > 0) {
      LOG_ERR("TLS", "%u wolfSSL blocks (%u bytes) still in the framebuffer at the end of the transfer",
              static_cast<unsigned>(info.allocated_blocks), static_cast<unsigned>(info.total_allocated_bytes));
    }
    LOG_DBG("TLS", "Scratch tail low-water %u of %u bytes; %u allocations fell back to the heap (largest %u)",
            static_cast<unsigned>(info.minimum_free_bytes), static_cast<unsigned>(g_tailLen),
            static_cast<unsigned>(g_fallbackCount), static_cast<unsigned>(g_fallbackLargest));
    g_tail = nullptr;
  }
  // g_tailBase / g_tailLen are deliberately left set: a free() that arrives
  // after the loan still has to be recognised as ours (see scratchFree).
  buildscratch::release(g_block);
  g_block = nullptr;
  g_blockLen = 0;
  active_ = false;
#endif
}

}  // namespace tls_scratch
