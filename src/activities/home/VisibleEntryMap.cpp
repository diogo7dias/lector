#include "VisibleEntryMap.h"

#include <algorithm>

bool VisibleEntryMap::reset(const size_t sourceCount) {
  removedBits_.fill(0);
  sourceCount_ = 0;
  removedCount_ = 0;
  if (sourceCount > large_folder_index::MAX_INDEX_ENTRIES) return false;
  sourceCount_ = sourceCount;
  return true;
}

bool VisibleEntryMap::removed(const size_t sourceIndex) const {
  const size_t wordIndex = sourceIndex / BITS_PER_WORD;
  const uint32_t mask = uint32_t{1} << (sourceIndex % BITS_PER_WORD);
  return (removedBits_[wordIndex] & mask) != 0;
}

size_t VisibleEntryMap::removedCount(uint32_t bits) {
  size_t total = 0;
  while (bits != 0) {
    bits &= bits - 1;
    ++total;
  }
  return total;
}

bool VisibleEntryMap::sourceIndexAt(const size_t visibleIndex, size_t& sourceIndex) const {
  if (visibleIndex >= count()) return false;
  if (removedCount_ == 0) {
    sourceIndex = visibleIndex;
    return true;
  }

  size_t remaining = visibleIndex;
  for (size_t wordIndex = 0; wordIndex < WORD_COUNT; ++wordIndex) {
    const size_t wordStart = wordIndex * BITS_PER_WORD;
    if (wordStart >= sourceCount_) break;
    const size_t validBits = std::min(BITS_PER_WORD, sourceCount_ - wordStart);
    const size_t visibleBits = validBits - removedCount(removedBits_[wordIndex]);
    if (remaining >= visibleBits) {
      remaining -= visibleBits;
      continue;
    }

    for (size_t bit = 0; bit < validBits; ++bit) {
      const size_t candidate = wordStart + bit;
      if (removed(candidate)) continue;
      if (remaining == 0) {
        sourceIndex = candidate;
        return true;
      }
      --remaining;
    }
  }
  return false;
}

bool VisibleEntryMap::eraseAt(const size_t visibleIndex) {
  size_t sourceIndex = 0;
  if (!sourceIndexAt(visibleIndex, sourceIndex)) return false;
  const size_t wordIndex = sourceIndex / BITS_PER_WORD;
  removedBits_[wordIndex] |= uint32_t{1} << (sourceIndex % BITS_PER_WORD);
  ++removedCount_;
  return true;
}
