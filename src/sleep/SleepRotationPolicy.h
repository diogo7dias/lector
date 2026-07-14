#pragma once

// Pure rotation-cursor policy for sleep wallpapers.
//
// Rotation walks a logical cursor 0,1,2,...,count-1 and maps each logical
// position to a physical index via the affine shuffle in
// large_folder_index::mapShuffledIndex (a coprime-multiplier permutation, so the
// mapping is a bijection over [0, count) and every physical slot is visited
// exactly once per lap — non-repeating with no stored permutation). When a lap
// completes the cursor reseeds with a fresh multiplier/offset, giving a new
// shuffled order each lap.
//
// This is the math core only: it takes no dependency on SD, the framebuffer, or
// the folder index, so it is fully host-testable. The device layer supplies the
// live file count and entropy, and resolves the physical index to a filename
// (skipping any file that vanished since the index was built).

#include <cstddef>
#include <cstdint>

#include "activities/home/LargeFolderIndexPolicy.h"

namespace sleep_rotation {

// Persisted across deep sleep in APP_STATE. `position` is the logical step within
// the current lap; `multiplier`/`offset` define the current lap's shuffle;
// `seededCount` is the file count the shuffle was seeded for — the affine map is
// only a permutation when gcd(multiplier, count) == 1, so any count change forces
// a reseed. `seeded` distinguishes a fresh install from a real state.
struct Cursor {
  uint32_t position = 0;
  uint32_t multiplier = 1;
  uint32_t offset = 0;
  uint32_t seededCount = 0;
  bool seeded = false;
};

// Choose a fresh lap order. `randA`/`randB` are entropy (esp_random() on device,
// fixed values in tests). Reseeds the shuffle and rewinds the cursor to 0.
inline void reseed(Cursor& c, const size_t count, const uint32_t randA, const uint32_t randB) {
  const size_t fileCount = count;
  c.multiplier = static_cast<uint32_t>(large_folder_index::coprimeMultiplier(randA, fileCount == 0 ? 1 : fileCount));
  c.offset = fileCount == 0 ? 0 : static_cast<uint32_t>(randB % fileCount);
  c.position = 0;
  c.seededCount = static_cast<uint32_t>(fileCount);
  c.seeded = true;
}

// True when the persisted cursor no longer matches the live folder (fresh
// install, or files added/removed since the lap was seeded).
inline bool needsReseed(const Cursor& c, const size_t count) {
  return !c.seeded || c.seededCount != count || c.multiplier == 0;
}

// Physical index (0-based record index in the folder index) to show for the
// current cursor position under the current lap's shuffle. Returns 0 when empty.
// Callers must reseed first when needsReseed() — this only clamps defensively.
inline size_t physicalIndex(const Cursor& c, const size_t count) {
  if (count == 0) return 0;
  const uint32_t pos = c.position >= count ? 0 : c.position;
  return large_folder_index::mapShuffledIndex(pos, count, /*firstFileIndex=*/0, c.multiplier, c.offset);
}

// Advance one step after a successful show. When the cursor reaches the end of
// the lap — or the live count no longer matches the seeded count — it reseeds
// for a fresh shuffled lap over the current folder size.
inline void advance(Cursor& c, const size_t count, const uint32_t randA, const uint32_t randB) {
  if (count == 0) {
    c = Cursor{};
    return;
  }
  if (needsReseed(c, count)) {
    reseed(c, count, randA, randB);
  }
  c.position += 1;
  if (c.position >= count) {
    reseed(c, count, randA, randB);  // lap complete → new shuffled lap
  }
}

}  // namespace sleep_rotation
