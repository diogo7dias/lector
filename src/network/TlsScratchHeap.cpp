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

// The lent framebuffer, registered whole as a heap of its own: record buffers
// and small allocations alike come out of it (TlsScratchLayout.h).
// Set only while a loan is running.
multi_heap_handle_t g_pool = nullptr;

// The block's address range, kept FOREVER and deliberately so: wolfSSL can free
// a buffer after the loan has ended, and handing the framebuffer's address to
// the real free() corrupts the system heap. Recognising the range for the rest
// of the run costs a pointer and a length and makes that impossible.
uint8_t* g_base = nullptr;
size_t g_len = 0;

// Diagnostics for the failure screen: how many allocations the loan could not
// serve and had to leave on the system heap, what they came to, and the largest
// of them. Zero means every byte wolfSSL asked for came out of the framebuffer.
uint32_t g_fallbackCount = 0;
uint32_t g_fallbackBytes = 0;
uint32_t g_fallbackLargest = 0;

// The allocation wolfSSL did not get. See oomCount() in the header: this is
// what separates an out-of-memory from an unreachable server, and the free
// heap has to be read here because nothing later still sees this moment.
uint32_t g_oomCount = 0;
uint32_t g_oomSize = 0;
uint32_t g_oomFreeHeap = 0;

bool inBlock(const void* ptr) {
  if (ptr == nullptr || g_base == nullptr) return false;
  const auto* p = static_cast<const uint8_t*>(ptr);
  return p >= g_base && p < g_base + g_len;
}

#if defined(FREEINK_NET_WOLFSSL)
bool g_installed = false;

void noteFallback(const size_t size) {
  g_fallbackCount++;
  g_fallbackBytes += static_cast<uint32_t>(size);
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
  if (g_pool) {
    if (void* p = multi_heap_malloc(g_pool, size)) return p;
    noteFallback(size);
  }
  return heapFallback(size);
}

void scratchFree(void* ptr) {
  if (ptr == nullptr) return;
  if (inBlock(ptr)) {
    // With the loan over, the bytes are the framebuffer's again and the pool's
    // bookkeeping is gone: there is nothing to give back and nothing leaks,
    // because the next Session registers the whole block afresh.
    if (g_pool) multi_heap_free(g_pool, ptr);
    return;
  }
  free(ptr);
}

void* scratchRealloc(void* ptr, size_t size) {
  if (inBlock(ptr)) {
    if (g_pool) {
      if (void* grown = multi_heap_realloc(g_pool, ptr, size)) return grown;
      // The pool cannot hold it any more: move the block out to the system
      // heap. Copy only what the old allocation actually held.
      const size_t had = multi_heap_get_allocated_size(g_pool, ptr);
      void* moved = heapFallback(size);
      if (!moved) return nullptr;
      memcpy(moved, ptr, had < size ? had : size);
      multi_heap_free(g_pool, ptr);
      noteFallback(size);
      return moved;
    }
    // The loan ended and its bookkeeping with it, so the old size is no longer
    // knowable and the bytes are about to be drawn over. Copy what the caller
    // asked for, bounded by the block, and leave the framebuffer alone.
    void* moved = heapFallback(size);
    if (!moved) return nullptr;
    const size_t available = static_cast<size_t>(g_base + g_len - static_cast<uint8_t*>(ptr));
    memcpy(moved, ptr, available < size ? available : size);
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

bool isActive() { return g_pool != nullptr; }

uint32_t heapFallbackCount() { return g_fallbackCount; }
uint32_t heapFallbackBytes() { return g_fallbackBytes; }
uint32_t heapFallbackLargest() { return g_fallbackLargest; }

size_t poolFreeBytes() { return g_pool ? multi_heap_free_size(g_pool) : 0; }
size_t poolLowWaterBytes() { return g_pool ? multi_heap_minimum_free_size(g_pool) : 0; }

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
  // Installed once and never taken back out: see g_base. Swapping the
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
  g_fallbackBytes = 0;
  g_fallbackLargest = 0;
  g_oomCount = 0;
  g_oomSize = 0;
  g_oomFreeHeap = 0;
  multi_heap_handle_t pool = multi_heap_register(block, len);
  if (!pool) {
    // Set nothing: a range recorded here would be claimed by inBlock() for the
    // rest of the run over memory the panel has already taken back.
    LOG_ERR("TLS", "Could not register the %u-byte scratch pool", static_cast<unsigned>(len));
    buildscratch::release(block);
    return;
  }
  g_base = block;
  g_len = len;
  g_pool = pool;
  active_ = true;
  LOG_DBG("TLS", "Lending %u bytes of framebuffer to wolfSSL (%u usable, %u left with both record buffers live)",
          static_cast<unsigned>(len), static_cast<unsigned>(multi_heap_free_size(g_pool)),
          static_cast<unsigned>(smallPoolBytes(len)));
#endif
}

Session::~Session() {
#if defined(FREEINK_NET_WOLFSSL)
  if (!active_) return;
  // Anything still held here is about to be drawn over. It cannot be rescued
  // -- wolfSSL owns the pointer -- but a silent corruption is worse than a
  // named one, and the count says which allocation to go and look for.
  multi_heap_info_t info = {};
  multi_heap_get_info(g_pool, &info);
  if (info.allocated_blocks > 0) {
    LOG_ERR("TLS", "%u wolfSSL blocks (%u bytes) still in the framebuffer at the end of the transfer",
            static_cast<unsigned>(info.allocated_blocks), static_cast<unsigned>(info.total_allocated_bytes));
  }
  LOG_DBG("TLS", "Scratch pool low-water %u of %u bytes; %u allocations (%u bytes) fell back to the heap (largest %u)",
          static_cast<unsigned>(info.minimum_free_bytes), static_cast<unsigned>(g_len),
          static_cast<unsigned>(g_fallbackCount), static_cast<unsigned>(g_fallbackBytes),
          static_cast<unsigned>(g_fallbackLargest));
  g_pool = nullptr;
  // g_base / g_len are deliberately left set: a free() that arrives after the
  // loan still has to be recognised as ours (see scratchFree).
  buildscratch::release(g_base);
  active_ = false;
#endif
}

}  // namespace tls_scratch
