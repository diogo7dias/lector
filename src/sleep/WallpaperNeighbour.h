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
 * is FsHelpers::fileListLessC, the same natural order the file browser and the
 * BMP viewer use, with a byte-order tie-break so names it calls equal (case,
 * leading zeros) still get a strict order and none is skipped.
 */
#pragma once

#include <FsHelpers.h>

#include <cstring>
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
  const auto less = [](const char* a, const char* b) {
    if (FsHelpers::fileListLessC(a, b)) return true;
    return !FsHelpers::fileListLessC(b, a) && strcmp(a, b) < 0;
  };
  std::string best;
  // `name` is NUL-terminated by every walk (SdSleepImageFs reads into a C buffer).
  auto consider = [&](const char* name, size_t) {
    if (forward) {
      if (!less(current.c_str(), name)) return;
      if (best.empty() || less(name, best.c_str())) best = name;
    } else {
      if (!less(name, current.c_str())) return;
      if (best.empty() || less(best.c_str(), name)) best = name;
    }
  };
  auto sink = sinkFrom(consider);
  fs.walk(dir, sink);
  return best;
}

}  // namespace sleep
}  // namespace crosspoint
