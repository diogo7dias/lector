/**
 * @file WallpaperNeighbour.h
 * @brief Find the next or previous wallpaper in a folder, in name order.
 *
 * Backs "flick through the folder" in the wallpaper viewer. The obvious
 * implementation — list the folder, sort it, index into it — costs one
 * std::string per file, which is thousands of allocations on a real /sleep
 * folder and will not fit the device's fragmented heap.
 *
 * This instead streams the folder once and keeps only the single best candidate
 * seen so far, so peak heap is two filenames regardless of folder size. Ordering
 * is plain byte-order on the basename, which is stable and needs no sort.
 */
#pragma once

#include <string>

#include "SleepImageMove.h"

namespace crosspoint {
namespace sleep {

// The wallpaper immediately after (forward) or before (!forward) `current` in
// name order, within `dir`. Returns empty when `current` is already the last or
// first — deliberately no wrap-around, so the viewer's "<" and ">" hints can tell
// the user honestly when there is nothing further that way.
inline std::string neighbourWallpaper(ISleepImageFs& fs, const char* dir, const std::string& current,
                                      const bool forward) {
  std::string best;
  auto consider = [&](const char* name, const size_t len) {
    const std::string_view candidate(name, len);
    if (forward) {
      if (candidate <= std::string_view(current)) return;
      if (best.empty() || candidate < std::string_view(best)) best.assign(candidate);
    } else {
      if (candidate >= std::string_view(current)) return;
      if (best.empty() || candidate > std::string_view(best)) best.assign(candidate);
    }
  };
  auto sink = sinkFrom(consider);
  fs.walk(dir, sink);
  return best;
}

}  // namespace sleep
}  // namespace crosspoint
