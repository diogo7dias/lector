#pragma once

#include <cstddef>
#include <numeric>

namespace large_folder_index {
// Medium wallpaper folders are faster and comfortably small in RAM. The SD
// index is intentionally reserved for genuinely large libraries because it
// rescans the directory and performs several fixed-record merge passes.
inline constexpr size_t IN_MEMORY_ENTRY_LIMIT = 1024;
inline constexpr size_t IN_MEMORY_NAME_BYTES_LIMIT = 64 * 1024;
inline constexpr size_t MAX_INDEX_ENTRIES = 20000;
inline constexpr size_t MAX_SEARCH_RESULTS = 512;
// Floor run size (original behavior) and the largest run the build tries first.
// A 500-byte Record makes each run buffer runRecords*500 bytes; 128 records is
// 64 KB, allocated only at file-browser time when no EPUB layout holds the heap.
// Bigger runs mean fewer external-merge passes; build() ladders down 128->64->32
// on OOM so a fragmented heap never regresses below the 32-record floor.
inline constexpr size_t SORT_RUN_RECORDS = 32;
inline constexpr size_t SORT_RUN_RECORDS_MAX = 128;

inline constexpr bool shouldUseSdIndex(const size_t acceptedEntries) { return acceptedEntries > IN_MEMORY_ENTRY_LIMIT; }

inline constexpr bool shouldUseSdIndex(const size_t acceptedEntries, const size_t retainedNameBytes) {
  return shouldUseSdIndex(acceptedEntries) || retainedNameBytes > IN_MEMORY_NAME_BYTES_LIMIT;
}

inline constexpr bool canAddEntry(const size_t currentEntries) { return currentEntries < MAX_INDEX_ENTRIES; }

inline size_t coprimeMultiplier(size_t candidate, const size_t modulus) {
  if (modulus <= 1) return 1;
  candidate %= modulus;
  if (candidate == 0) candidate = 1;
  while (std::gcd(candidate, modulus) != 1) {
    candidate = (candidate + 1) % modulus;
    if (candidate == 0) candidate = 1;
  }
  return candidate;
}

inline constexpr size_t mapShuffledIndex(const size_t logicalIndex, const size_t totalCount,
                                         const size_t firstFileIndex, const size_t multiplier, const size_t offset) {
  if (logicalIndex < firstFileIndex || firstFileIndex >= totalCount) return logicalIndex;
  const size_t fileCount = totalCount - firstFileIndex;
  if (fileCount <= 1) return logicalIndex;
  const size_t relative = logicalIndex - firstFileIndex;
  return firstFileIndex + (multiplier * relative + offset) % fileCount;
}
}  // namespace large_folder_index
