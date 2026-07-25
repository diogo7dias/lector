/**
 * @file SleepImageMove.h
 * @brief Bulk-move sleep wallpapers between /sleep and "/sleep pause", filtered
 *        by favorite state.
 *
 * Backs the Settings actions that move favorites (or non-favorites) out of the
 * rotation folder and back again. The folder can hold thousands of images, so
 * this must never materialize every filename at once — that single allocation
 * is exactly what fails on the device's fragmented heap.
 *
 * It therefore works in bounded passes: each pass streams the source folder once
 * and copies at most `batchSize` matching names, then renames that batch, then
 * repeats until a pass finds no more matches. Files are never renamed WHILE the
 * directory walk is in flight — that would invalidate the SD directory iterator.
 * Peak heap is one batch of names, not the folder size.
 *
 * Pure and host-testable: the only seam is ISleepImageFs. Production passes
 * SdSleepImageFs (HalStorage); host tests pass a fake backed by a vector. The
 * favorite predicate needs no seam at all — it is a pure filename suffix test.
 */
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "util/FavoriteImageNames.h"

namespace crosspoint {
namespace sleep {

// Non-owning callback handed to ISleepImageFs::walk. A plain context pointer
// plus a function pointer, deliberately not std::function: the callback is
// invoked once per file in a folder of thousands, and std::function would add a
// heap-allocated closure and several KB of binary per instantiation.
struct NameSink {
  void* ctx = nullptr;
  void (*fn)(void*, const char* name, size_t len) = nullptr;
  void operator()(const char* name, size_t len) const { fn(ctx, name, len); }
};

// Wraps any lambda as a NameSink. The lambda must outlive the walk — always
// call this on a named local, never on a temporary.
template <class F>
NameSink sinkFrom(F& f) {
  return NameSink{&f, [](void* ctx, const char* name, size_t len) { (*static_cast<F*>(ctx))(name, len); }};
}

struct ISleepImageFs {
  virtual ~ISleepImageFs() = default;

  // Stream every wallpaper (.bmp / .pxc) directly inside `dir`, in SD iteration
  // order. `name` points at storage owned by the callee for the duration of the
  // call only, so the sink must copy anything it retains.
  virtual void walk(const char* dir, const NameSink& sink) = 0;
  virtual bool mkdir(const char* path) = 0;
  virtual bool rename(const char* from, const char* to) = 0;
};

struct MoveReport {
  size_t moved = 0;
  size_t failed = 0;
  // A pass matched files but moved none of them (a name collision in the
  // destination, or an SD error). The run stops rather than re-selecting the
  // same un-movable files forever, and this flags that it stopped early.
  bool stalled = false;
};

// Count wallpapers directly under `dir` whose favorite state equals `favorites`.
// Stops once `scanCap` matches are seen, which bounds worst-case time and keeps
// the number in the confirmation prompt sane on a huge folder.
inline size_t countImagesByFavorite(ISleepImageFs& fs, const char* dir, bool favorites, size_t scanCap) {
  size_t found = 0;
  auto count = [&](const char* name, size_t len) {
    if (found >= scanCap) return;
    if (FavoriteImage::hasFavoriteSuffix(std::string(name, len)) == favorites) ++found;
  };
  auto sink = sinkFrom(count);
  fs.walk(dir, sink);
  return found;
}

// Move every wallpaper under `fromDir` whose favorite state equals
// `moveFavorites` into `toDir`, in bounded passes. `yieldFn` (nullable) is
// invoked every `yieldEvery` moves so the caller can feed the watchdog during a
// long run.
inline MoveReport moveImagesByFavorite(ISleepImageFs& fs, const char* fromDir, const char* toDir, bool moveFavorites,
                                       size_t batchSize, size_t yieldEvery, void (*yieldFn)()) {
  MoveReport report;
  if (batchSize == 0) batchSize = 1;

  bool destDirReady = false;
  for (;;) {
    std::vector<std::string> batch;
    batch.reserve(batchSize);
    auto collect = [&](const char* name, size_t len) {
      if (batch.size() >= batchSize) return;
      std::string nm(name, len);
      if (FavoriteImage::hasFavoriteSuffix(nm) == moveFavorites) batch.push_back(std::move(nm));
    };
    auto sink = sinkFrom(collect);
    fs.walk(fromDir, sink);
    if (batch.empty()) break;

    // Created lazily, so a run that matches nothing never leaves an empty folder
    // behind.
    if (!destDirReady) {
      fs.mkdir(toDir);
      destDirReady = true;
    }

    size_t movedThisPass = 0;
    size_t failedThisPass = 0;
    for (const auto& nm : batch) {
      const std::string from = std::string(fromDir) + "/" + nm;
      const std::string to = std::string(toDir) + "/" + nm;
      if (fs.rename(from.c_str(), to.c_str())) {
        ++report.moved;
        ++movedThisPass;
        if (yieldFn != nullptr && yieldEvery != 0 && (report.moved % yieldEvery == 0)) yieldFn();
      } else {
        ++failedThisPass;
      }
    }
    // Overwritten, not accumulated: a file that fails to move stays in fromDir
    // and is re-selected by the next pass, so accumulating would count the same
    // stuck file once per pass. The last pass's figure is the true stuck count.
    report.failed = failedThisPass;
    if (movedThisPass == 0) {
      report.stalled = true;
      break;
    }
  }
  return report;
}

}  // namespace sleep
}  // namespace crosspoint
