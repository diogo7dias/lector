/**
 * @file LayoutHeapGate.h
 * @brief Pure heap-affordability predicate for the chapter layout path.
 *
 * Laying out a parse chunk allocates per-word vectors, a TextBlock arena and a
 * fresh Page. On a low, fragmented heap one of those allocations can fail; the
 * firmware is built with -fno-exceptions, so a failed allocation that is not
 * routed through a nothrow + null-check path terminates and abort()s. This
 * predicate lets the parser check the heap BEFORE feeding the next chunk and
 * stop the build cleanly (with a distinguishable low-memory signal) instead of
 * risking that abort.
 *
 * Pure and host-testable (no Arduino / heap_caps), mirroring WholeFileReadGate.
 *
 * Thresholds were shipped conservative (16 KB / 8 KB) while a trip still
 * ended the build as a hard error. Both conditions changed: the render
 * fallback ladder retries a tripped build at a degraded tier, and live X3
 * session logs (2026-07-16) showed the old gate letting builds run to a
 * 3.8 KB largest block — deep inside abort territory (per-paragraph scratch
 * arenas alone want 4 KB, TextBlock arenas and glyph loads ride on top).
 * Raised to 24 KB free / 16 KB largest: the ladder engages while allocations
 * still succeed, instead of after paragraphs start dropping.
 */
#pragma once

#include <cstddef>

namespace crosspoint {

// True when the heap is too low or too fragmented to safely lay out another
// chapter chunk. `freeHeap` is the total free heap; `largestFreeBlock` is the
// largest contiguous allocatable block (fragmentation, not just total, is what
// actually fails an allocation).
inline bool layoutHeapCritical(size_t freeHeap, size_t largestFreeBlock) {
  constexpr size_t kMinFreeHeap = 24 * 1024;
  constexpr size_t kMinLargestFreeBlock = 16 * 1024;
  return freeHeap < kMinFreeHeap || largestFreeBlock < kMinLargestFreeBlock;
}

}  // namespace crosspoint
