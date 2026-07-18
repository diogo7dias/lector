/**
 * @file Wallpaper.h
 * @brief Free-function facade over the sleep wallpaper rotation (RFC #145).
 *
 * Hides the V2 impl class, the production fs/IFileIO adapters, the APP_STATE
 * pointer wiring, and the deep-sleep flush sequencing. Production callers use
 * the namespaced free functions; tests opt in via Configure().
 *
 * Lazy init: on first call from any entry point, production deps are wired
 * inside this module. main.cpp does not need to call any setup function.
 *
 * Migration window: V1 (WallpaperPlaylist) and V2 (WallpaperPlaylistV2) both
 * remain in tree; this facade currently routes only to V2 (which is the
 * default for every build env in platformio.ini). V1 deletion is a follow-up.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "SleepFs.h"
#include "persist/IFileIO.h"

namespace crosspoint {
namespace sleep {
namespace wallpaper {

// Legacy V2 hard cap. The index rotation engine no longer trims /sleep to this;
// it survives only as the V2 class's internal constant during the migration
// window. UI warnings key off kSleepIndexMaxImages below instead.
constexpr size_t kSleepFolderCap = 500;

// Ceiling of the index rotation engine (mirrors windex::kMaxEntries /
// large_folder_index::MAX_INDEX_ENTRIES). /sleep may hold up to this many
// images; picks stay O(1) regardless of count.
constexpr size_t kSleepIndexMaxImages = 20000;

// Outcome of nextSleepFile() — everything the caller needs to render the
// picked wallpaper without consulting APP_STATE or recomputing the
// favorite display name.
struct SleepPick {
  std::string fullPath;     // "/sleep/foo.bmp", "/sleep.pxc", or empty if none
  std::string basename;     // basename portion; empty for paused / root-fallback
  std::string displayName;  // FavoriteImage::displayNameForPath(fullPath)
  bool isPaused = false;    // facade routed via paused-rotation branch
  bool isFallback = false;  // facade routed via /sleep.{pxc,bmp} root fallback

  bool hasImage() const { return !fullPath.empty(); }
};

// Caller-supplied render attempt. Returns true if `pick.fullPath` was
// rendered successfully. Returning false signals a transient open/parse
// failure — the facade will pick another candidate and call probe again
// up to an internal retry budget.
using RenderProbe = std::function<bool(const SleepPick&)>;

// Hot-path entry point that hides heap-fragmentation strategy, the
// paused-rotation branch, the playlist-vs-direct-pick decision, the
// parse/open retry loop, and the post-render lastShown bookkeeping
// (RFC #156). Picks the next wallpaper given current heap state and
// on-disk content, drives `probe` until one renders successfully or the
// retry budget is exhausted.
//
// Returns the SleepPick that ultimately rendered (fullPath non-empty)
// or an empty pick if /sleep is empty or every retry failed.
//
// During the migration window this coexists with `advance()`; both
// share the same playlist state. New code should prefer this entry
// point.
SleepPick nextSleepFile(const RenderProbe& probe);

// Cold path: mark /sleep as needing a reconcile. Cheap. Reconcile work
// runs lazily inside the next advance() under the rich-sleep heap-budget
// gate. New files dropped via USB transfer become visible on the next
// sleep cycle, not the home screen immediately after disconnect.
void markFolderDirty();

// Settings-screen "shuffle now" — force a reshuffle. Returns false when
// /sleep is empty, true when at least one file was selected.
bool reshuffle();

// Post-render: remember the actually-rendered path (paused-mode dedup).
// `filename` is the basename portion; pass empty to leave lastShown alone.
void rememberRendered(const std::string& fullPath, const std::string& filename = "");

// Direct fs handle for callers that need to enumerate /sleep themselves
// (e.g. fragment-safe direct pick). Never null after first lazy init.
ISleepFs* fs();

// Count images currently in /sleep, bounded by `scanCap` (worst-case time).
// Used by the home over-limit indicator. Returns 0 if fs is unavailable.
size_t countImages(size_t scanCap);

// Move up to `n` randomly-chosen images from /sleep to "/sleep pause". Streams
// the folder via reservoir sampling (O(n) heap), so it stays memory-safe on a
// folder of 1000+ images. Favorites are included. Marks the rotation dirty so it
// rebuilds on the next sleep. Returns the number of files actually moved.
size_t moveRandomToPause(size_t n);

// Count /sleep images whose favorite state matches `favorites` (true = favorites,
// false = non-favorites), bounded by `scanCap`. Backs the Settings confirmation
// prompt ("Move N wallpapers?"). Returns 0 if fs is unavailable.
size_t countByFavorite(bool favorites, size_t scanCap);

// Called periodically during a bulk move with the running count of files moved
// so far, so the caller can update an on-screen "Moving... N" banner. A large
// move is otherwise silent for many seconds and looks stuck.
using ProgressFn = std::function<void(size_t movedSoFar)>;

// Move every /sleep image whose favorite state matches `favorites` into
// "/sleep pause". Works in bounded batches (memory-safe on 1000+ image folders)
// and yields to the watchdog during the run. `onProgress` (nullable) fires every
// few files with the running count. Marks the rotation dirty so it rebuilds on
// the next sleep. Returns the number of files actually moved.
size_t moveToPauseByFavorite(bool favorites, const ProgressFn& onProgress = nullptr);

// Move every favorite image in "/sleep pause" back into "/sleep" — the reverse
// of moveToPauseByFavorite(true). Works in bounded batches (memory-safe on
// 1000+ image folders) and yields to the watchdog during the run. `onProgress`
// (nullable) fires every few files with the running count. Marks the rotation
// dirty so it rebuilds on the next sleep. Returns files actually moved.
size_t moveFavoritesToSleep(const ProgressFn& onProgress = nullptr);

// No-op in the V2 default path — reconcile is heap-gated and runs from
// inside advance(). Kept so the boot-route hook + ActivityRouter signature
// stay valid without conditional plumbing at the call site.
void reconcileIfDirty();

// Test/power-user opt-in. Production callers do not invoke this; the lazy
// init wires production adapters automatically. Tests call resetForTest()
// then Configure() with fakes.
struct Config {
  ISleepFs* fs = nullptr;
  persist::IFileIO* fileIO = nullptr;
  std::string orderFilePath = "/.crosspoint/sleep_order.txt";

  std::string* lastShownFilename = nullptr;
  std::string* lastRenderedPath = nullptr;

  std::function<bool()> saveAppState;
  std::function<long(long)> randomFn;
  std::function<bool(const std::string&)> isFavorite;
  std::function<void(const std::string& /*from*/, const std::string& /*to*/)> onPathRenamed;
  // Optional host-test seam: returns the largest contiguous free heap block
  // in bytes. Production lazy init wires heap_caps_get_largest_free_block
  // (MALLOC_CAP_DEFAULT) automatically; tests inject a stub to simulate
  // fragmentation cliffs. If left unset, the playlist treats the heap as
  // unlimited — matches the pre-RFC #156 host behaviour.
  std::function<size_t()> largestFreeBlockFn;
  // Maps a /sleep basename to its favorite-toggle counterpart (x.bmp <->
  // x_F.bmp). Lets reconcile recognize a favorite rename as an in-place name
  // change rather than a fresh upload. Production lazy init wires the
  // FavoriteImage suffix helpers automatically; unset disables rename folding.
  std::function<std::string(const std::string&)> favoriteCounterpartFn;
};

void Configure(const Config&);
void resetForTest();

}  // namespace wallpaper
}  // namespace sleep
}  // namespace crosspoint
