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
 * Thresholds are deliberately CONSERVATIVE. Today a trip ends the build as a
 * clean error with no automatic quality fallback, so the gate must never fire
 * during normal reading — only near genuine starvation. Once the render
 * fallback ladder lands (a trip degrades page quality and retries instead of
 * failing), these can be raised to catch OOM more proactively.
 */
#pragma once

#include <cstddef>

namespace crosspoint {

// True when the heap is too low or too fragmented to safely lay out another
// chapter chunk. `freeHeap` is the total free heap; `largestFreeBlock` is the
// largest contiguous allocatable block (fragmentation, not just total, is what
// actually fails an allocation).
inline bool layoutHeapCritical(size_t freeHeap, size_t largestFreeBlock) {
  constexpr size_t kMinFreeHeap = 16 * 1024;
  constexpr size_t kMinLargestFreeBlock = 8 * 1024;
  return freeHeap < kMinFreeHeap || largestFreeBlock < kMinLargestFreeBlock;
}

}  // namespace crosspoint
