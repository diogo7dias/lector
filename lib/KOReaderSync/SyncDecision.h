#pragma once

#include <cmath>

/**
 * The smart-sync merge rule, kept apart from the activity so it can be read and
 * tested on its own.
 *
 * There is no timestamp in the comparison on purpose. The reader has no
 * battery-backed clock: its time is only set by NTP inside a sync, so the local
 * progress file carries no trustworthy write time to compare a server timestamp
 * against. The rule is therefore "furthest position wins", with the guard that
 * a remote percentage that is not a usable fraction never overwrites the local
 * position -- a broken or empty server record heals from the device instead.
 */
namespace kosync {

enum class MergeChoice {
  ALREADY_SYNCED,  // Both sides are at the same place; nothing to do.
  UPLOAD_LOCAL,    // Local is further, or the remote value is unusable.
  APPLY_REMOTE     // Remote is further; adopt it.
};

// Positions within this fraction of each other count as the same position.
// 0.001 is 0.1 percentage points: under half a page in a 400-page book.
inline constexpr float SAME_PROGRESS_EPSILON = 0.001f;

inline bool isUsablePercentage(const float percentage) {
  return std::isfinite(percentage) && percentage >= 0.0f && percentage <= 1.0f;
}

inline MergeChoice decideMerge(const float localPercentage, const float remotePercentage) {
  // A remote record that is not a fraction cannot be mapped to a position, so
  // it must never replace one. Upload instead: the local position is the only
  // one known good, and sending it repairs the server's record.
  if (!isUsablePercentage(remotePercentage)) return MergeChoice::UPLOAD_LOCAL;
  if (!isUsablePercentage(localPercentage)) return MergeChoice::APPLY_REMOTE;

  const float delta = localPercentage - remotePercentage;
  if (std::fabs(delta) <= SAME_PROGRESS_EPSILON) return MergeChoice::ALREADY_SYNCED;
  return delta > 0.0f ? MergeChoice::UPLOAD_LOCAL : MergeChoice::APPLY_REMOTE;
}

}  // namespace kosync
