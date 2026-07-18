/**
 * @file SleepFavoriteMove.h
 * @brief Bulk-move /sleep wallpapers to /sleep pause, filtered by favorite state.
 *
 * Backs the two Settings actions "move favorites to /sleep pause" and "move
 * non-favorites to /sleep pause". Like SleepMoveSelection, it must operate on a
 * folder that can hold hundreds of images WITHOUT materializing every name at
 * once — the full vector of names is exactly the allocation that trips bad_alloc
 * on the device's fragmented ~160 KB heap.
 *
 * It therefore works in bounded passes: each pass streams /sleep once and copies
 * at most `batchSize` matching names, then renames that batch out of /sleep, then
 * repeats until a pass finds no more matches. It never renames a file WHILE the
 * directory walk is in flight (that would invalidate the SD directory iterator) —
 * the batch is collected first, then moved. Peak heap is `batchSize` names, not
 * the folder size.
 *
 * Pure and host-testable: production feeds an SdFatSleepFs and the real favorite
 * predicate; tests feed a FakeSleepFs and a std::set.
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "SleepFs.h"

namespace crosspoint {
namespace sleep {

// True iff `name` (a basename) should be moved this run: its favorite state,
// per isFavorite, equals the requested target (moveFavorites).
using IsFavoriteFn = std::function<bool(const std::string& /*name*/)>;
// Fired after each successful rename so callers can repoint any references.
using OnRenamedFn = std::function<void(const std::string& /*from*/, const std::string& /*to*/)>;
// Streams the source folder once per call, invoking `cb(name, len, mtime)` per
// image. Lets the mover work in either direction (/sleep or /sleep pause) by
// swapping which folder walk it is handed.
using WalkFn =
    std::function<void(const std::function<void(const char* /*name*/, size_t /*len*/, uint32_t /*mtime*/)>&)>;

// Count .bmp/.pxc files directly under /sleep whose favorite state matches
// `favorites`. Stops once `scanCap` matches are seen (bounds worst-case time and
// keeps the confirmation prompt's number sane on a huge folder).
inline size_t countSleepImagesByFavorite(ISleepFs& fs, const IsFavoriteFn& isFavorite, bool favorites, size_t scanCap) {
  size_t n = 0;
  fs.walkSleepBmps([&](const char* name, size_t len, uint32_t) {
    if (n >= scanCap) return;
    const std::string nm(name, len);
    const bool fav = isFavorite ? isFavorite(nm) : false;
    if (fav == favorites) ++n;
  });
  return n;
}

// Move every image under `fromDir` whose favorite state equals `moveFavorites`
// into `toDir`, in bounded passes (<= batchSize names held at once). `walk`
// streams `fromDir` once per pass. Returns the number of files actually moved.
// `onRenamed` fires per successful move; `yieldFn` (nullable) is invoked every
// `yieldEvery` moves so the caller can feed the watchdog / yield the CPU during
// a long run. A pass that matches files but moves none of them (e.g. a name
// collision in `toDir`) stops the loop so a permanently un-movable file can
// never spin forever.
inline size_t moveImagesByFavorite(const WalkFn& walk, ISleepFs& fs, const IsFavoriteFn& isFavorite,
                                   const OnRenamedFn& onRenamed, bool moveFavorites, const std::string& fromDir,
                                   const std::string& toDir, size_t batchSize, size_t yieldEvery,
                                   const std::function<void()>& yieldFn) {
  if (batchSize == 0) batchSize = 1;
  size_t total = 0;
  bool destDirReady = false;
  for (;;) {
    std::vector<std::string> batch;
    batch.reserve(batchSize);
    walk([&](const char* name, size_t len, uint32_t) {
      if (batch.size() >= batchSize) return;
      std::string nm(name, len);
      const bool fav = isFavorite ? isFavorite(nm) : false;
      if (fav == moveFavorites) batch.push_back(std::move(nm));
    });
    if (batch.empty()) break;

    if (!destDirReady) {
      fs.mkdir(toDir);
      destDirReady = true;
    }

    size_t movedThisPass = 0;
    for (const auto& nm : batch) {
      const std::string from = fromDir + "/" + nm;
      const std::string to = toDir + "/" + nm;
      if (fs.rename(from, to)) {
        if (onRenamed) onRenamed(from, to);
        ++total;
        ++movedThisPass;
        if (yieldFn && yieldEvery && (total % yieldEvery == 0)) yieldFn();
      }
    }
    // No progress this pass: the remaining matches cannot be moved (name clash in
    // the destination, or fs error). Stop rather than re-select them forever.
    if (movedThisPass == 0) break;
  }
  return total;
}

// Move every /sleep image whose favorite state equals `moveFavorites` into
// /sleep pause. Thin wrapper over moveImagesByFavorite using the /sleep walk.
inline size_t moveSleepImagesByFavorite(ISleepFs& fs, const IsFavoriteFn& isFavorite, const OnRenamedFn& onRenamed,
                                        bool moveFavorites, size_t batchSize, size_t yieldEvery,
                                        const std::function<void()>& yieldFn) {
  return moveImagesByFavorite(
      [&fs](const std::function<void(const char*, size_t, uint32_t)>& cb) { fs.walkSleepBmps(cb); }, fs, isFavorite,
      onRenamed, moveFavorites, "/sleep", "/sleep pause", batchSize, yieldEvery, yieldFn);
}

// Move every /sleep pause image whose favorite state equals `moveFavorites` back
// into /sleep. Backs the "move favorites to /sleep" Settings action (the reverse
// of the pause moves). Streams /sleep pause, so it stays memory-safe on a folder
// of thousands of images.
inline size_t moveSleepPauseImagesByFavorite(ISleepFs& fs, const IsFavoriteFn& isFavorite, const OnRenamedFn& onRenamed,
                                             bool moveFavorites, size_t batchSize, size_t yieldEvery,
                                             const std::function<void()>& yieldFn) {
  return moveImagesByFavorite(
      [&fs](const std::function<void(const char*, size_t, uint32_t)>& cb) { fs.walkPauseBmps(cb); }, fs, isFavorite,
      onRenamed, moveFavorites, "/sleep pause", "/sleep", batchSize, yieldEvery, yieldFn);
}

}  // namespace sleep
}  // namespace crosspoint
