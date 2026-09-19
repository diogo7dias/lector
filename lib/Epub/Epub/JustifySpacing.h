#pragma once

#include <cstddef>

// Consume extras only at eligible gaps, in the same order as word positioning.
class JustifySpacing {
 public:
  static constexpr size_t MIN_JUSTIFY_GAPS = 1;

  JustifySpacing(const int spareSpace, const size_t gapCount)
      : totalExtra(gapCount >= MIN_JUSTIFY_GAPS && spareSpace > 0 ? spareSpace : 0),
        perGap(totalExtra ? totalExtra / static_cast<int>(gapCount) : 0),
        remainder(totalExtra ? totalExtra % static_cast<int>(gapCount) : 0) {}

  int nextExtra() {
    if (remainder > 0) {
      --remainder;
      return perGap + 1;
    }
    return perGap;
  }

  const int totalExtra;

 private:
  // Sparse lines legitimately need large gaps: never cap the per-gap stretch.
  const int perGap;
  int remainder;
};
