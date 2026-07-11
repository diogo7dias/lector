#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "LargeFolderIndexPolicy.h"

// Bounded view over fixed index coordinates. Erasing a visible row only marks
// its source coordinate; every surviving row keeps its original source index.
class VisibleEntryMap {
 public:
  bool reset(size_t sourceCount);

  size_t count() const { return sourceCount_ - removedCount_; }
  bool sourceIndexAt(size_t visibleIndex, size_t& sourceIndex) const;
  bool eraseAt(size_t visibleIndex);

 private:
  static constexpr size_t BITS_PER_WORD = 32;
  static constexpr size_t WORD_COUNT = (large_folder_index::MAX_INDEX_ENTRIES + BITS_PER_WORD - 1) / BITS_PER_WORD;

  std::array<uint32_t, WORD_COUNT> removedBits_{};
  size_t sourceCount_ = 0;
  size_t removedCount_ = 0;

  bool removed(size_t sourceIndex) const;
  static size_t removedCount(uint32_t bits);
};
