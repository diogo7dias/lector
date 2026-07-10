#include "Wallpaper.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <esp_heap_caps.h>

#include "../CrossPointState.h"
#include "../util/FavoriteImage.h"
#include "SdFatSleepFs.h"
#include "SleepMoveSelection.h"
#include "WallpaperPlaylistV2.h"
#include "persist/SdFatFileIOLite.h"

namespace crosspoint {
namespace sleep {
namespace wallpaper {

namespace {

// Function-local statics: zero-init in BSS, constructed on first touch
// after Arduino framework init has run. Avoids static-init-order hazards
// with file-scope globals.
SdFatSleepFs& defaultFs() {
  static SdFatSleepFs s;
  return s;
}

persist::SdFatFileIOLite& defaultFileIO() {
  static persist::SdFatFileIOLite s;
  return s;
}

bool s_configured = false;

// Wire production deps onto the V2 impl. Idempotent: the configured flag
// short-circuits subsequent calls. Tests that want to override deps call
// resetForTest() first to drop the flag.
void ensureConfigured() {
  if (s_configured) return;
  s_configured = true;

  v2::WallpaperPlaylistV2::Deps d;
  d.fs = &defaultFs();
  d.fileIO = &defaultFileIO();
  d.lastShownFilename = &APP_STATE.lastShownSleepFilename;
  d.lastRenderedPath = &APP_STATE.lastSleepWallpaperPath;
  // advance() runs inside SleepActivity::onEnter, milliseconds before the CPU
  // enters deep sleep, so we force a synchronous flush so rotation state
  // survives the deep-sleep boundary.
  d.saveAppState = []() { return APP_STATE.saveToFile(); };
  d.randomFn = [](long mod) -> long { return ::random(mod); };
  d.isFavorite = [](const std::string& path) { return FavoriteImage::isFavoritePath(path); };
  d.onPathRenamed = [](const std::string& from, const std::string& to) {
    FavoriteImage::replacePathReferences(from, to);
  };
  // Lets reconcile fold a favorite/unfavorite rename (x.bmp <-> x_F.bmp) back
  // into its rotation slot instead of treating the renamed file as a fresh
  // upload and re-showing it on the next lock.
  d.favoriteCounterpartFn = [](const std::string& name) -> std::string {
    return FavoriteImage::hasFavoriteSuffix(name) ? FavoriteImage::stripFavoriteSuffix(name)
                                                  : FavoriteImage::addFavoriteSuffix(name);
  };
  // Heap probe: the playlist's inner -fno-exceptions gates use this to bail
  // before any std::string::reserve that might bad_alloc-abort on a fragmented
  // sleep-entry heap.
  d.largestFreeBlockFn = []() { return heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT); };

  v2::WallpaperPlaylistV2::instance().setDeps(d);
}

// Drain the reconcile notice and fold it into persistent APP_STATE so the
// next-wake home screen can warn / toast. The favorites-cap flag is sticky
// state (refreshed only when a reconcile actually ran); the moved-to-pause
// count is a transient event the home screen consumes once. Persists only when
// something changed — overflow is rare, so the extra synchronous flush before
// deep sleep stays off the common path.
void applyReconcileNotice() {
  const auto n = v2::WallpaperPlaylistV2::instance().takeNotice();
  bool changed = false;
  if (n.reconciled && APP_STATE.sleepFavoritesCapReached != n.favoritesCapBlocked) {
    APP_STATE.sleepFavoritesCapReached = n.favoritesCapBlocked;
    changed = true;
  }
  if (n.movedToPause > 0 && APP_STATE.pendingSleepWallpapersMovedToPause < 9999) {
    APP_STATE.pendingSleepWallpapersMovedToPause += n.movedToPause;
    changed = true;
  }
  if (changed) {
    APP_STATE.saveToFile();
  }
}

}  // namespace

std::string advance() {
  ensureConfigured();
  const std::string pick = v2::WallpaperPlaylistV2::instance().advance();
  applyReconcileNotice();
  return pick;
}

namespace {

// Retry budget for nextSleepFile's probe loop.
constexpr int kNextSleepFileRetries = 5;

// Streaming fragment-safe pick — O(1) heap beyond the returned basename.
// DETERMINISTIC SEQUENTIAL: returns the lexicographically next .bmp/.pxc
// strictly after `after`, wrapping to the lex-first at the end of the lap.
// `after` is the just-shown basename (persisted across deep sleep), so
// successive wakes walk the folder one file at a time in a fixed order.
std::string pickDirectBasename(const std::string& after) {
  auto* sfs = v2::WallpaperPlaylistV2::instance().deps().fs;
  if (!sfs) return {};
  return sfs->nextSleepBmpAfter(after);
}

SleepPick makePickFromBasename(const std::string& basename) {
  SleepPick p;
  if (basename.empty()) return p;
  p.basename = basename;
  p.fullPath = "/sleep/" + basename;
  p.displayName = FavoriteImage::displayNameForPath(p.fullPath);
  return p;
}

}  // namespace

SleepPick nextSleepFile(const RenderProbe& probe) {
  ensureConfigured();
  if (!probe) return SleepPick{};

  // Paused-rotation branch: re-show the previously rendered wallpaper without
  // advancing the playlist cursor. The "/sleep/" prefix guard covers a paused
  // wallpaper demoted to /sleep pause (trim, quick-move): the reference fixup
  // repoints lastSleepWallpaperPath to the pause folder, and re-showing from
  // there would defeat the move — fall through to normal rotation instead.
  if (APP_STATE.wallpaperRotationPaused && APP_STATE.lastSleepWallpaperPath.rfind("/sleep/", 0) == 0 &&
      Storage.exists(APP_STATE.lastSleepWallpaperPath.c_str())) {
    SleepPick paused;
    paused.fullPath = APP_STATE.lastSleepWallpaperPath;
    paused.displayName = FavoriteImage::displayNameForPath(paused.fullPath);
    paused.isPaused = true;
    if (probe(paused)) {
      return paused;
    }
    // Paused render failed (file vanished, parse error, etc.). Fall through to
    // the normal pick path so the user still sees an image.
  }

  // Always attempt the buffer-backed advance()/reconcile() first: it honors the
  // shuffled + newest-first order and now heap-gates its own load/rebuild/splice
  // internally, returning empty (→ direct pick below) if the fragmented heap
  // can't afford a given step. The former up-front affordability gate assumed a
  // full rebuild every wake, so it perpetually forced the ordering-blind direct
  // pick — which is exactly why shuffle and new-on-top appeared to do nothing.
  const bool useDirectPick = false;

  // Sequential cursor for the direct-pick path: its OWN persisted field, NOT
  // lastShownSleepFilename. The buffer engine writes lastShownSleepFilename on
  // its picks; sharing it let a buffer wake reset the direct walk to an unrelated
  // lex position, so at ~400 files (where the heap gate flips wake-to-wake) the
  // rotation clustered on a shifting few and never cycled the rest. Keeping the
  // direct cursor separate lets each engine advance its own progress.
  std::string directAfter = APP_STATE.lastDirectPickFilename;

  for (int attempt = 0; attempt < kNextSleepFileRetries; ++attempt) {
    std::string basename;
    bool pickedDirect = useDirectPick;
    if (useDirectPick) {
      basename = pickDirectBasename(directAfter);
    } else {
      // Buffer-backed playlist advance via the facade entry point, so the
      // reconcile notice is drained + persisted here too. advance() can still
      // bail to empty under its internal heap probes — fall through to direct.
      basename = advance();
      if (basename.empty()) {
        basename = pickDirectBasename(directAfter);
        pickedDirect = true;  // buffer engine bailed — this came from the direct walk
      }
    }
    if (basename.empty()) {
      break;  // /sleep is empty — no point retrying.
    }
    SleepPick pick = makePickFromBasename(basename);
    if (probe(pick)) {
      // Persist the direct walk's own cursor only when this pick actually came
      // from it, so a buffer-engine pick never moves it. Set before
      // rememberRendered() so its APP_STATE save (fullPath always changes on a
      // new pick) writes this field too — no extra SD write.
      // Persist the direct-pick cursor advance. rememberRendered() below saves
      // APP_STATE only when the shown file/path changes; when the SAME file is
      // re-picked it skips the save, stranding this cursor update in RAM where the
      // deep-sleep reset loses it — so the walk lands on that same file every wake
      // (the favorite-successor freeze). Detect the unchanged-render case and force
      // the save so the cursor always advances across sleep.
      const bool renderedFileUnchanged =
          pick.fullPath == APP_STATE.lastSleepWallpaperPath && pick.basename == APP_STATE.lastShownSleepFilename;
      if (pickedDirect) APP_STATE.lastDirectPickFilename = pick.basename;
      v2::WallpaperPlaylistV2::instance().rememberRendered(pick.fullPath, pick.basename);
      if (pickedDirect && renderedFileUnchanged) APP_STATE.saveToFile();
      return pick;
    }
    // Probe rejected this candidate. Step the direct-pick cursor past it so the
    // next retry returns the following file in sequence, not the same one.
    directAfter = basename;
  }

  // Root-level fallback ladder: /sleep is empty or every candidate failed to
  // render. PXC takes precedence over BMP.
  for (const char* fallbackPath : {"/sleep.pxc", "/sleep_F.bmp", "/sleep.bmp"}) {
    if (!Storage.exists(fallbackPath)) continue;
    SleepPick fb;
    fb.fullPath = fallbackPath;
    fb.displayName = FavoriteImage::displayNameForPath(fallbackPath);
    fb.isFallback = true;
    if (probe(fb)) {
      v2::WallpaperPlaylistV2::instance().rememberRendered(fb.fullPath, "");
      return fb;
    }
  }

  return SleepPick{};
}

void markFolderDirty() {
  ensureConfigured();
  v2::WallpaperPlaylistV2::instance().markFolderDirty();
}

bool reshuffle() {
  ensureConfigured();
  return v2::WallpaperPlaylistV2::instance().reshuffle();
}

void rememberRendered(const std::string& fullPath, const std::string& filename) {
  ensureConfigured();
  v2::WallpaperPlaylistV2::instance().rememberRendered(fullPath, filename);
}

ISleepFs* fs() {
  ensureConfigured();
  return v2::WallpaperPlaylistV2::instance().deps().fs;
}

size_t countImages(size_t scanCap) {
  ensureConfigured();
  ISleepFs* sfs = v2::WallpaperPlaylistV2::instance().deps().fs;
  return sfs ? sfs->countSleepBmps(scanCap) : 0;
}

size_t moveRandomToPause(size_t n) {
  ensureConfigured();
  if (n == 0) return 0;
  const auto& deps = v2::WallpaperPlaylistV2::instance().deps();
  ISleepFs* sfs = deps.fs;
  if (!sfs) return 0;

  // No usable randomness → fall back to a deterministic "keep first n" pick so
  // the action still works (production always wires deps.randomFn).
  RandomFn rnd = deps.randomFn ? deps.randomFn : [](long) -> long { return 0; };

  // Stream /sleep once, reservoir-sampling n names — never materializes the
  // whole folder. `name` points at the SD layer's stack buffer; copy it in.
  Reservoir reservoir(n, rnd);
  sfs->walkSleepBmps(
      [&reservoir](const char* name, size_t len, uint32_t /*mtime*/) { reservoir.offer(std::string(name, len)); });

  const std::vector<std::string>& picks = reservoir.take();
  if (picks.empty()) return 0;

  sfs->mkdir("/sleep pause");
  size_t moved = 0;
  for (const auto& nm : picks) {
    const std::string from = "/sleep/" + nm;
    const std::string to = "/sleep pause/" + nm;
    if (sfs->rename(from, to)) {
      if (deps.onPathRenamed) deps.onPathRenamed(from, to);
      ++moved;
    }
  }
  if (moved > 0) v2::WallpaperPlaylistV2::instance().markFolderDirty();
  return moved;
}

void reconcileIfDirty() {
  // V2: reconcile is heap-gated and runs lazily inside advance(). No-op here.
}

void Configure(const Config& c) {
  s_configured = true;

  v2::WallpaperPlaylistV2::Deps d;
  d.fs = c.fs;
  d.fileIO = c.fileIO;
  d.orderFilePath = c.orderFilePath;
  d.lastShownFilename = c.lastShownFilename;
  d.lastRenderedPath = c.lastRenderedPath;
  d.saveAppState = c.saveAppState;
  d.randomFn = c.randomFn;
  d.isFavorite = c.isFavorite;
  d.onPathRenamed = c.onPathRenamed;
  d.largestFreeBlockFn = c.largestFreeBlockFn;
  d.favoriteCounterpartFn = c.favoriteCounterpartFn;

  v2::WallpaperPlaylistV2::instance().setDeps(d);
}

void resetForTest() {
  v2::WallpaperPlaylistV2::instance().resetForTest();
  s_configured = false;
}

}  // namespace wallpaper
}  // namespace sleep
}  // namespace crosspoint
