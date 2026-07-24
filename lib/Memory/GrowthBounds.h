#pragma once

#include <cstddef>

// Overflow-safe bound check for appending to a size-capped file/buffer.
// canGrowWithinLimit(current, added, limit) is true only when `current` is
// already within `limit` AND `added` still fits under it — computed without
// `current + added` so it never overflows size_t.
namespace memory {

inline constexpr bool canGrowWithinLimit(const size_t current, const size_t added, const size_t limit) {
  return current <= limit && added <= limit - current;
}

}  // namespace memory
