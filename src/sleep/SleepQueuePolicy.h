#pragma once

// Pure pick policy over the sleep wallpaper index: fresh queue first, then the
// shuffled lap.
//
// The index file is append-only between rebuilds, so the records split into two
// contiguous regions:
//   [0, cursor.seededCount)      — the current lap's domain (shuffled walk)
//   [freshNext, recordCount)     — the fresh region: records appended since the
//                                  lap was seeded, served next, in append order
// Serving order per pick: drain the fresh region, then resume the lap. When the
// lap wraps (every seeded record shown once) the cursor reseeds over the full
// record count and the fresh region folds into the new lap — loop completion IS
// the automatic reshuffle.
//
// Records can go stale between rebuilds (file deleted, moved to /sleep pause,
// or renamed by a favorite toggle), so each candidate is verified through
// `exists` and — for favorite renames — its counterpart name, preserving the
// image's rotation slot.
//
// Cursor semantics: the state is advanced PAST every slot this call inspects,
// including the returned candidate. The caller persists the state only after a
// successful render, so a crash or render failure simply re-walks from the last
// persisted position. Dead slots consumed while skipping stay consumed, so a
// stale index cannot make every lock re-pay the same skips after a save.
//
// Templated on the accessors (no std::function: this runs on the sleep-entry
// heap) and free of SD/display dependencies, so it is host-testable with fakes.

#include <cstdint>
#include <string>

#include "SleepRotationPolicy.h"

namespace sleep_queue {

// Upper bound of dead slots tolerated per pick before declaring the index stale.
inline constexpr size_t kMaxDeadSlotSkips = 64;

// Persisted in APP_STATE alongside the cursor.
struct QueueState {
  sleep_rotation::Cursor cursor;
  uint32_t freshNext = 0;  // next fresh record to serve; == recordCount when drained
};

struct Result {
  std::string basename;       // next live wallpaper; empty when none found
  bool needsRebuild = false;  // exhausted the skip budget — index is stale
  bool lapWrapped = false;    // this pick completed a lap (every record shown once)
};

// Manual "Shuffle Wallpapers": a fresh lap over everything, fresh queue folded in.
inline void reshuffle(QueueState& s, const size_t recordCount, const uint32_t randA, const uint32_t randB) {
  sleep_rotation::reseed(s.cursor, recordCount, randA, randB);
  s.freshNext = static_cast<uint32_t>(recordCount);
}

// nameAt(size_t physicalIndex) -> std::string  (empty = unreadable record)
// exists(const std::string& basename) -> bool  (file present in the sleep dir)
// counterpart(const std::string& basename) -> std::string  (favorite-toggled
//   name, e.g. x.pxc <-> x_F.pxc; empty = no counterpart)
template <typename NameAtFn, typename ExistsFn, typename CounterpartFn>
Result pickNext(QueueState& s, const size_t recordCount, const uint32_t randA, const uint32_t randB, NameAtFn&& nameAt,
                ExistsFn&& exists, CounterpartFn&& counterpart) {
  Result result;
  if (recordCount == 0) return result;
  if (s.freshNext > recordCount) s.freshNext = static_cast<uint32_t>(recordCount);  // defensive clamp

  size_t budget = recordCount < kMaxDeadSlotSkips ? recordCount : kMaxDeadSlotSkips;

  const auto tryCandidate = [&](std::string&& name) -> bool {
    if (name.empty()) return false;
    if (exists(name)) {
      result.basename = std::move(name);
      return true;
    }
    std::string alt = counterpart(name);
    if (!alt.empty() && exists(alt)) {
      result.basename = std::move(alt);
      return true;
    }
    return false;
  };

  // Fresh phase: appended records jump the queue, in append order.
  while (s.freshNext < recordCount && budget > 0) {
    const size_t idx = s.freshNext;
    s.freshNext += 1;  // step past unconditionally (crash-safe re-walk convention)
    --budget;
    if (tryCandidate(nameAt(idx))) return result;
  }

  // Lap phase.
  auto& cursor = s.cursor;
  if (sleep_rotation::needsReseed(cursor, recordCount)) {
    sleep_rotation::reseed(cursor, recordCount, randA, randB);
    s.freshNext = static_cast<uint32_t>(recordCount);  // a reseed always folds the fresh region
  }
  for (size_t i = 0; budget > 0; ++i, --budget) {
    const size_t physical = sleep_rotation::physicalIndex(cursor);
    std::string name = nameAt(physical);
    // Step past this slot unconditionally; vary the entropy so a reseed at a
    // lap wrap inside the skip walk does not repeat the same shuffle.
    const bool wrapped = sleep_rotation::advance(cursor, recordCount, randA ^ static_cast<uint32_t>(i + 1),
                                                 randB + static_cast<uint32_t>(i));
    if (wrapped) {
      s.freshNext = static_cast<uint32_t>(recordCount);  // lap complete → fresh folded into new lap
      result.lapWrapped = true;  // sticky: a wrap consumed while skipping dead slots still counts
    }
    if (tryCandidate(std::move(name))) return result;
  }
  result.needsRebuild = true;
  return result;
}

}  // namespace sleep_queue
