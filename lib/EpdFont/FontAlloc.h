#pragma once

// PSRAM-preferring allocator for font scratch that is large and latency tolerant (decompressed
// glyph pages, the hot group buffer). On the X4 Pro (BOARD_HAS_PSRAM) the block comes from PSRAM,
// falling back to the internal heap when PSRAM is absent or full. Other boards, and host builds,
// get plain malloc. fontFree releases either region, so callers need not know which they got.
//
// The x4pro sdkconfig already sends any malloc over 4 KB to PSRAM (SPIRAM_USE_MALLOC with
// MALLOC_ALWAYSINTERNAL=4096); this makes the placement explicit and independent of that setting,
// and lifts blocks under 4 KB as well. Keep small, hot buffers on plain malloc.

#include <cstddef>
#include <cstdlib>

#if defined(BOARD_HAS_PSRAM) && defined(ARDUINO)
#include <esp_heap_caps.h>
#endif

inline void* fontMalloc(const size_t size) {
#if defined(BOARD_HAS_PSRAM) && defined(ARDUINO)
  if (void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) return p;
#endif
  return malloc(size);
}

inline void fontFree(void* ptr) { free(ptr); }  // heap_caps_free is free() on ESP-IDF
