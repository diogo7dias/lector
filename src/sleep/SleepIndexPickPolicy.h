#pragma once

// Pure candidate-pick policy over the sleep wallpaper index.
//
// Given the persisted rotation cursor and the index size, walks the shuffled lap
// (SleepRotationPolicy) to the next LIVE wallpaper: index records can go stale
// between rebuilds (file deleted, moved to /sleep pause, or renamed by a
// favorite toggle), so each candidate is verified through `exists` and — for
// favorite renames — its counterpart name, preserving the image's rotation slot.
//
// Cursor semantics: the cursor is advanced PAST every slot this call inspects,
// including the returned candidate. The caller persists the cursor only after a
// successful render, so a crash or render failure simply re-walks from the last
// persisted position. Dead slots consumed while skipping stay consumed, so a
// stale index cannot make every lock re-pay the same skips after a save.
//
// Templated on the accessors (no std::function: this runs on the sleep-entry
// heap) and free of SD/display dependencies, so it is host-testable with fakes.

#include <cstdint>
#include <string>

#include "SleepRotationPolicy.h"

namespace sleep_index_pick {

// Upper bound of dead slots tolerated per pick before declaring the index stale.
inline constexpr size_t kMaxDeadSlotSkips = 64;

struct Result {
  std::string basename;       // next live wallpaper; empty when none found
  bool needsRebuild = false;  // exhausted the skip budget — index is stale
};

// nameAt(size_t physicalIndex) -> std::string  (empty = unreadable record)
// exists(const std::string& basename) -> bool  (file present in /sleep)
// counterpart(const std::string& basename) -> std::string  (favorite-toggled
//   name, e.g. x.pxc <-> x_F.pxc; empty = no counterpart)
template <typename NameAtFn, typename ExistsFn, typename CounterpartFn>
Result pickNext(sleep_rotation::Cursor& cursor, const size_t count, const uint32_t randA, const uint32_t randB,
                NameAtFn&& nameAt, ExistsFn&& exists, CounterpartFn&& counterpart) {
  Result result;
  if (count == 0) return result;
  if (sleep_rotation::needsReseed(cursor, count)) {
    sleep_rotation::reseed(cursor, count, randA, randB);
  }

  const size_t attempts = count < kMaxDeadSlotSkips ? count : kMaxDeadSlotSkips;
  for (size_t i = 0; i < attempts; ++i) {
    const size_t physical = sleep_rotation::physicalIndex(cursor, count);
    std::string name = nameAt(physical);
    // Step past this slot unconditionally; vary the entropy so a reseed at a
    // lap wrap inside the skip walk does not repeat the same shuffle.
    sleep_rotation::advance(cursor, count, randA ^ static_cast<uint32_t>(i + 1), randB + static_cast<uint32_t>(i));
    if (name.empty()) continue;
    if (exists(name)) {
      result.basename = std::move(name);
      return result;
    }
    std::string alt = counterpart(name);
    if (!alt.empty() && exists(alt)) {
      result.basename = std::move(alt);
      return result;
    }
  }
  result.needsRebuild = true;
  return result;
}

}  // namespace sleep_index_pick
