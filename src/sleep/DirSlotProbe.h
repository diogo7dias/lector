#pragma once

// Slot-level probing of a FAT directory through SdFat's seek + openNext.
//
// A FAT directory is a flat array of fixed-size 32-byte slots, and SdFat's
// openNext() reads from the directory's current position: it returns the first
// live entry at or after wherever the position was left. That makes two cheap
// operations legal without walking the folder:
//   - "is there anything at or after slot N?"  (entryExistsAt)
//   - "how many slots up to the last live entry?"  (liveSlotCount, a doubling
//     probe + binary search, ~2*log2(entries) reads)
// The sleep screen's jump pick uses both to land on a random wallpaper; the
// wallpaper index reconcile uses liveSlotCount as the denominator of its
// progress bar, because the walk's byte position over the slot span IS the
// scan's real progress.
//
// Shared here so the two callers cannot drift; extracted verbatim from the
// jump pick in SleepActivity.cpp.

#include <HalStorage.h>

#include <cstddef>

namespace crosspoint {
namespace sleep {

constexpr size_t DIR_SLOT_BYTES = 32;
// 131072 slots — far past any real wallpaper folder, and only a bound for the
// doubling probe, not an allocation.
constexpr size_t MAX_DIR_BYTES = 4u * 1024u * 1024u;

// True when at least one live directory entry exists at or after `offset`.
// Monotone in `offset` (entries are contiguous and terminated by a free slot),
// which is what makes the binary search below valid.
inline bool entryExistsAt(HalFile& dir, const size_t offset) {
  if (!dir.seekSet(offset)) return false;
  auto probe = dir.openNextFile();
  const bool found = static_cast<bool>(probe);
  if (probe) probe.close();
  return found;
}

// Slot count up to and including the last live entry, or 0 for an empty
// directory. Leaves `dir` at an arbitrary position — rewind before walking.
inline size_t liveSlotCount(HalFile& dir) {
  if (!entryExistsAt(dir, 0)) return 0;

  // Grow a bound until a probe lands past the last entry.
  size_t lastLive = 0;
  size_t past = DIR_SLOT_BYTES;
  while (past < MAX_DIR_BYTES && entryExistsAt(dir, past)) {
    lastLive = past;
    past *= 2;
  }
  // Narrow to the last slot that still yields an entry.
  while (past - lastLive > DIR_SLOT_BYTES) {
    const size_t mid = ((lastLive + (past - lastLive) / 2) / DIR_SLOT_BYTES) * DIR_SLOT_BYTES;
    if (mid == lastLive) break;
    if (entryExistsAt(dir, mid)) {
      lastLive = mid;
    } else {
      past = mid;
    }
  }
  return lastLive / DIR_SLOT_BYTES + 1;
}

}  // namespace sleep
}  // namespace crosspoint
