#include "HeapFailureProbe.h"

#include <esp_heap_caps.h>

namespace heap_probe {
namespace {

// Written from the failing allocator's context, which can be an ISR and can be
// a task that holds the heap lock, so: DRAM (never a flash const), no
// allocation, no heap queries, no logging.
DRAM_ATTR volatile uint32_t g_failures = 0;
DRAM_ATTR volatile uint32_t g_firstSize = 0;
DRAM_ATTR volatile uint32_t g_largestSize = 0;

bool g_installed = false;

void IRAM_ATTR onAllocFailed(const size_t size, uint32_t /*caps*/, const char* /*functionName*/) {
  const uint32_t seen = g_failures;
  if (seen == 0) g_firstSize = static_cast<uint32_t>(size);
  // Read then write, rather than ++: a read-modify-write on a volatile is
  // deprecated in C++20 and warns. Single writer, so no increment is lost.
  g_failures = seen + 1;
  if (size > g_largestSize) g_largestSize = static_cast<uint32_t>(size);
}

}  // namespace

void arm() {
  g_failures = 0;
  g_firstSize = 0;
  g_largestSize = 0;
  if (g_installed) return;
  // Left installed for the rest of the run: registering is idempotent in
  // effect, and a counter that is only cleared costs nothing between transfers.
  g_installed = heap_caps_register_failed_alloc_callback(onAllocFailed) == ESP_OK;
}

Record read() { return Record{g_failures, g_firstSize, g_largestSize}; }

}  // namespace heap_probe
