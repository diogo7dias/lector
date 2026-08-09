#pragma once

// Pure rotation-cursor policy for sleep wallpapers.
//
// Rotation walks a logical cursor 0,1,2,...,lapCount-1 and maps each logical
// position to a physical index via an affine shuffle (a coprime-multiplier
// permutation, so the mapping is a bijection over [0, lapCount) and every
// physical slot is visited exactly once per lap — non-repeating with no stored
// permutation). When a lap completes the cursor reseeds with a fresh
// multiplier/offset over the CURRENT record count, giving a new shuffled order
// each lap that folds in any records appended mid-lap.
//
// Ported from the pre-rebase lector line (see git ffa834bd) with two changes:
// the affine helpers are inlined here (their old home,
// activities/home/LargeFolderIndexPolicy.h, does not exist on this branch) and
// advance() takes the lap domain and the reseed domain separately so appends
// never restart a lap in flight.
//
// This is the math core only: it takes no dependency on SD, the framebuffer, or
// the index file, so it is fully host-testable. The device layer supplies the
// live record count and entropy, and resolves the physical index to a filename
// (skipping any file that vanished since the index was built).

#include <cstddef>
#include <cstdint>
#include <numeric>

namespace sleep_rotation {

// Smallest multiplier >= candidate (mod modulus) that is coprime with modulus,
// so (multiplier * pos + offset) % modulus permutes [0, modulus).
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

// Affine bijection: logical lap position -> physical record index.
inline constexpr size_t mapShuffledIndex(const size_t logicalIndex, const size_t count, const size_t multiplier,
                                         const size_t offset) {
  if (count <= 1) return 0;
  return (multiplier * logicalIndex + offset) % count;
}

// Persisted across deep sleep in APP_STATE. `position` is the logical step within
// the current lap; `multiplier`/`offset` define the current lap's shuffle;
// `seededCount` is the record count the shuffle was seeded for — the affine map
// is only a permutation over [0, seededCount), so the lap domain is exactly the
// records that existed at seed time. `seeded` distinguishes a fresh install from
// a real state.
struct Cursor {
  uint32_t position = 0;
  uint32_t multiplier = 1;
  uint32_t offset = 0;
  uint32_t seededCount = 0;
  bool seeded = false;
};

// Choose a fresh lap order over `count` records. `randA`/`randB` are entropy
// (esp_random() on device, fixed values in tests). Rewinds the cursor to 0.
inline void reseed(Cursor& c, const size_t count, const uint32_t randA, const uint32_t randB) {
  c.multiplier = static_cast<uint32_t>(coprimeMultiplier(randA, count == 0 ? 1 : count));
  c.offset = count == 0 ? 0 : static_cast<uint32_t>(randB % count);
  c.position = 0;
  c.seededCount = static_cast<uint32_t>(count);
  c.seeded = true;
}

// True when the persisted cursor cannot drive a lap: fresh install, corrupt
// state, or the index shrank below the seeded domain (only a rebuild shrinks
// the index, and a rebuild reseeds — so this is purely defensive).
inline bool needsReseed(const Cursor& c, const size_t recordCount) {
  return !c.seeded || c.multiplier == 0 || c.seededCount == 0 || c.seededCount > recordCount;
}

// Physical record index for the current cursor position under the current
// lap's shuffle. The lap domain is c.seededCount, NOT the live record count:
// records appended mid-lap are served by the fresh queue and only join the lap
// at the next reseed. Returns 0 when empty. Callers must reseed first when
// needsReseed() — this only clamps defensively.
inline size_t physicalIndex(const Cursor& c) {
  if (c.seededCount == 0) return 0;
  const uint32_t pos = c.position >= c.seededCount ? 0 : c.position;
  return mapShuffledIndex(pos, c.seededCount, c.multiplier, c.offset);
}

// Advance one step after inspecting a slot. When the cursor reaches the end of
// the lap it reseeds over `reseedCount` (the CURRENT record count) — lap
// completion is the automatic reshuffle, and it folds in appended records.
// Returns true when the lap wrapped (callers fold the fresh queue then).
inline bool advance(Cursor& c, const size_t reseedCount, const uint32_t randA, const uint32_t randB) {
  if (c.seededCount == 0) {
    reseed(c, reseedCount, randA, randB);
    return true;
  }
  c.position += 1;
  if (c.position >= c.seededCount) {
    reseed(c, reseedCount, randA, randB);  // lap complete → new shuffled lap over everything
    return true;
  }
  return false;
}

}  // namespace sleep_rotation
