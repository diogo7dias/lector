#pragma once

#include <cstddef>

namespace memory {
inline constexpr bool canGrowWithinLimit(const size_t current, const size_t added, const size_t limit) {
  return current <= limit && added <= limit - current;
}
}  // namespace memory
