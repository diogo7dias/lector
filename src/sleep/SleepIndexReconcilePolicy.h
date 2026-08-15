#pragma once

// Pure decision logic for the cold-boot wallpaper index reconcile.
//
// The device layer walks the sleep folder once (pass A) computing a live count
// and an order-independent fingerprint. When they match the persisted snapshot
// and nothing marked the index dirty, the index is trusted as-is. Otherwise the
// index records are hashed into a transient set (pass B) and the folder is
// walked again (pass C): names absent from the set — and whose favorite-rename
// counterpart is also absent, so a favorite toggle never appends a duplicate —
// are appended to the index as the new fresh region.
//
// Host-testable: no SD, no display, no ESP headers.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace sleep_reconcile {

// Hard cap carried over from the pre-rebase index store (record slots).
inline constexpr uint32_t kMaxEntries = 20000;
// Above this the incremental path's transient hash set stops being worth its
// RAM; fall back to a full rebuild.
inline constexpr uint32_t kIncrementalMaxEntries = 10000;
// Dead slots tolerated before a compacting full rebuild: max(64, count/4).
inline constexpr uint32_t kDeadSlotFloor = 64;

// Order-independent folder fingerprint: the uint32 wrap-sum of entryHash across
// all wallpaper entries. Per-entry hash mixes name, mtime AND size — mtime alone
// is weak because macOS Finder and Windows Explorer preserve source mtimes on
// copy, so a same-name replacement could otherwise go unseen.
inline uint32_t entryHash(const std::string_view name, const uint32_t mtime, const uint32_t size) {
  uint32_t h = 2166136261u;  // FNV-1a 32
  for (const char ch : name) {
    h ^= static_cast<uint8_t>(ch);
    h *= 16777619u;
  }
  h ^= mtime;
  h *= 16777619u;
  h ^= size;
  h *= 16777619u;
  return h;
}

// 64-bit name-only hash for the membership set. 64 bits because a 32-bit space
// gives ~1% odds of some colliding pair across a 10k-file folder's lifetime,
// and a collided new file would silently never index; at 64 bits the risk is
// gone for 8 bytes per record of transient RAM (~80 KB at the cap).
inline uint64_t nameHash(const std::string_view name) {
  uint64_t h = 14695981039346656037ull;  // FNV-1a 64
  for (const char ch : name) {
    h ^= static_cast<uint8_t>(ch);
    h *= 1099511628211ull;
  }
  return h;
}

// Sorted-vector set of nameHash values. Build with add(), then finalize() once,
// then query with contains(). ~8 bytes per record, freed when it goes out of
// scope; no per-node heap churn like a std::set.
class NameHashSet {
 public:
  void reserve(const size_t n) { hashes.reserve(n); }
  void add(const uint64_t h) { hashes.push_back(h); }
  void finalize() {
    std::sort(hashes.begin(), hashes.end());
    hashes.erase(std::unique(hashes.begin(), hashes.end()), hashes.end());
  }
  bool contains(const uint64_t h) const { return std::binary_search(hashes.begin(), hashes.end(), h); }
  size_t size() const { return hashes.size(); }

 private:
  std::vector<uint64_t> hashes;
};

// Persisted folder snapshot the boot gate compares against. A device-side
// mutation (delete, single-file add) is fully described by one entryHash, so it
// patches this in place instead of marking the folder dirty — the next boot
// then matches the snapshot and skips the walk entirely.
struct Snapshot {
  uint32_t fingerprint = 0;
  uint32_t liveCount = 0;
  uint32_t deadSlots = 0;  // records with no live file behind them
};

// A wallpaper left the folder: drop its contribution, leave its record behind
// as a hole the pick skips. The fingerprint is a wrap-sum, so the subtraction
// is exact even across uint32 overflow.
inline Snapshot applyDeletion(Snapshot s, const uint32_t entryHash) {
  s.fingerprint -= entryHash;
  if (s.liveCount > 0) --s.liveCount;
  ++s.deadSlots;
  return s;
}

// A wallpaper arrived and its record was appended at end of file: no hole, so
// the dead-slot count is untouched.
inline Snapshot applyAddition(Snapshot s, const uint32_t entryHash) {
  s.fingerprint += entryHash;
  ++s.liveCount;
  return s;
}

// Holes cost one skipped record per pick (budget kMaxDeadSlotSkips) and dead
// weight in the index file, so they are tolerated up to the same ratio the
// walking reconcile uses. Past it the index needs compacting.
inline bool deadSlotsDemandRebuild(const uint32_t recordCount, const uint32_t deadSlots) {
  return deadSlots > std::max(kDeadSlotFloor, recordCount / 4);
}

enum class Plan : uint8_t {
  NoChange,           // index trusted as-is, nothing written
  IncrementalAppend,  // hash records, append unknown folder names at EOF
  FullRebuild,        // rebuild the index from scratch (tmp + rotate)
};

struct DecideInput {
  bool indexUsable = false;       // file present, header magic/version ok
  bool dirChanged = false;        // resolved sleep dir != indexed dir
  bool dirty = false;             // a mutation hook flagged the folder
  bool needsRebuildFlag = false;  // pick exhausted its skip budget earlier
  uint32_t scannedLive = 0;       // pass A: wallpapers in the folder now
  uint32_t scannedFingerprint = 0;
  uint32_t snapLive = 0;  // persisted snapshot from the last reconcile
  uint32_t snapFingerprint = 0;
  uint32_t recordCount = 0;     // records currently in the index file
  uint32_t pendingAppends = 0;  // pass C: unknown names found (0 before pass C)
};

// Dead slots = records (plus pending appends) with no live file behind them.
inline uint32_t deadSlots(const DecideInput& in) {
  const uint32_t total = in.recordCount + in.pendingAppends;
  return total > in.scannedLive ? total - in.scannedLive : 0;
}

// Called twice by the device layer: once after pass A (pendingAppends = 0) to
// pick the cheap path, and once after pass C to decide whether the appends
// pushed the index over a cap or the dead-slot ratio demands compaction.
inline Plan decidePlan(const DecideInput& in) {
  if (!in.indexUsable || in.dirChanged || in.needsRebuildFlag) return Plan::FullRebuild;
  // An empty index with a populated folder is a first fill, not an append: a
  // rebuild gives the shuffled first lap; an append would serve file order.
  if (in.recordCount == 0 && in.scannedLive > 0) return Plan::FullRebuild;
  if (in.recordCount > kIncrementalMaxEntries) return Plan::FullRebuild;
  if (in.recordCount + in.pendingAppends > kMaxEntries) return Plan::FullRebuild;
  if (deadSlots(in) > std::max(kDeadSlotFloor, in.recordCount / 4)) return Plan::FullRebuild;
  if (!in.dirty && in.scannedLive == in.snapLive && in.scannedFingerprint == in.snapFingerprint) return Plan::NoChange;
  return Plan::IncrementalAppend;
}

}  // namespace sleep_reconcile
