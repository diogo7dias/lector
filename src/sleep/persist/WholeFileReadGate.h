/**
 * @file WholeFileReadGate.h
 * @brief Heap-affordability gate for reading a whole file into a std::string.
 *
 * readWhole() loads an entire file into an Arduino String and then copies it
 * into a std::string, so peak transient heap is ~2x the file size, and every
 * allocation is THROWING. The firmware is built with -fno-exceptions, so a
 * failed operator new does not return null — it terminates and abort()s. This
 * runs at sleep entry on a low, fragmented heap (loading the wallpaper order
 * file on lock), which is exactly where a large file made it crash.
 *
 * This pure predicate lets the read bail before allocating when the heap cannot
 * comfortably hold the file, so the caller degrades to the O(1)-heap direct pick
 * instead of aborting. Pure and host-testable (no Arduino / heap_caps).
 */
#pragma once

#include <cstddef>

namespace crosspoint {
namespace persist {

// True when it is safe to read a file of `fileSize` bytes wholly into memory,
// given the largest contiguous free heap block. Requires headroom for the
// Arduino String buffer, the std::string copy, and growth slack; refuses an
// implausibly large file outright.
inline bool wholeFileReadAffordable(size_t fileSize, size_t largestFreeBlock) {
  // No order/config file on this device is legitimately this large; refuse
  // rather than attempt a huge allocation (also guards the multiply below).
  constexpr size_t kMaxWholeFileBytes = size_t{1} << 20;  // 1 MiB
  if (fileSize > kMaxWholeFileBytes) return false;
  // Arduino String buffer (~fileSize) + std::string copy (~fileSize) live at the
  // same time, plus allocator slack; require the largest block to hold ~3x.
  const size_t need = fileSize * 3 + 512;
  return largestFreeBlock >= need;
}

}  // namespace persist
}  // namespace crosspoint
