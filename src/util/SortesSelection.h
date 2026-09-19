#pragma once

#include <cstdint>

namespace sortes {
using Random = uint32_t (*)();

// Rejection sampling: every value below bound has the same probability.
inline uint32_t below(uint32_t bound, Random random) {
  if (bound == 0) return 0;
  const uint32_t threshold = -bound % bound;
  uint32_t value;
  do {
    value = random();
  } while (value < threshold);
  return value % bound;
}

// Streaming reservoir of size one. Unknown/malformed markers are ineligible too.
inline bool consider(int percent, uint32_t& eligibleCount, Random random) {
  if (percent != 100) return false;
  return below(++eligibleCount, random) == 0;
}
}  // namespace sortes
