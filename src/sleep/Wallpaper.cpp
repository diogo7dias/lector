#include "Wallpaper.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_task_wdt.h>

#include <string>
#include <vector>

#include "../CrossPointState.h"
#include "../util/FavoriteImage.h"
#include "SdFatSleepFs.h"
#include "SleepFavoriteMove.h"
#include "SleepIndexPickPolicy.h"
#include "SleepMoveSelection.h"
#include "SleepWallpaperIndexStore.h"
#include "WallpaperDirectPickPolicy.h"
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

// Retry budget for nextSleepFile's probe loop.
constexpr int kNextSleepFileRetries = 5;

SleepPick makePickFromBasename(const std::string& basename) {
  SleepPick p;
  if (basename.empty()) return p;
  p.basename = basename;
  p.fullPath = "/sleep/" + basename;
  p.displayName = FavoriteImage::displayNameForPath(p.fullPath);
  return p;
}

// ── Index-engine cursor bridge (SleepRotationPolicy <-> APP_STATE) ───────────
// The shuffled-lap cursor lives in state.json so it survives the deep-sleep
// power cut. Loaded into the pure Cursor struct for the pick, stored back
// before the single post-render state flush.

sleep_rotation::Cursor loadCursor() {
  sleep_rotation::Cursor c;
  c.position = APP_STATE.sleepCursorPos;
  c.multiplier = APP_STATE.sleepCursorMult;
  c.offset = APP_STATE.sleepCursorOff;
  c.seededCount = APP_STATE.sleepCursorSeededCount;
  c.seeded = APP_STATE.sleepCursorSeeded;
  return c;
}

void storeCursor(const sleep_rotation::Cursor& c) {
  APP_STATE.sleepCursorPos = c.position;
  APP_STATE.sleepCursorMult = c.multiplier;
  APP_STATE.sleepCursorOff = c.offset;
  APP_STATE.sleepCursorSeededCount = c.seededCount;
  APP_STATE.sleepCursorSeeded = c.seeded;
}

}  // namespace

SleepPick nextSleepFile(const RenderProbe& probe) {
  ensureConfigured();
  if (!probe) return SleepPick{};

  // Paused-rotation branch: re-show the previously rendered wallpaper without
  // advancing the rotation cursor. The "/sleep/" prefix guard covers a paused
  // wallpaper demoted to /sleep pause (quick-move): the reference fixup
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

  // ── Index engine (the only rotation engine) ────────────────────────────────
  // Picks are O(1) reads from the prebuilt /.crosspoint/sleep_index.bin — no
  // directory scan on this user-blocking path. The index is normally rebuilt by
  // the chunked idle pump while the user reads; a missing/stale index (first
  // boot after upgrade, format switch, uploads with no idle window) falls back
  // to ONE blocking WDT-yielded build here — still a single scan, not the old
  // O(N²) per-name walk.
  windex::Snapshot snap{APP_STATE.sleepIndexCount, APP_STATE.sleepIndexFingerprint};
  windex::Reader reader;
  bool rebuilt = false;
  if (windex::isDirty() || snap.count == 0 || !reader.open()) {
    rebuilt = windex::buildBlocking(snap);
    if (rebuilt) reader.open();
  }

  sleep_rotation::Cursor cursor = loadCursor();
  const auto entropy = []() { return static_cast<uint32_t>(esp_random()); };

  for (int attempt = 0; attempt < kNextSleepFileRetries && snap.count > 0; ++attempt) {
    auto picked = sleep_index_pick::pickNext(
        cursor, snap.count, entropy(), entropy(), [&](size_t i) { return reader.nameAt(i); },
        [&](const std::string& n) { return Storage.exists(("/sleep/" + n).c_str()); },
        [](const std::string& n) -> std::string {
          return FavoriteImage::hasFavoriteSuffix(n) ? FavoriteImage::stripFavoriteSuffix(n)
                                                     : FavoriteImage::addFavoriteSuffix(n);
        });
    if (picked.needsRebuild && !rebuilt) {
      // Too many dead records: the folder changed a lot since the index was
      // built. Rebuild once and keep picking.
      rebuilt = windex::buildBlocking(snap);
      if (!rebuilt || !reader.open()) break;
      cursor = loadCursor();
      continue;
    }
    if (picked.basename.empty()) break;

    SleepPick pick = makePickFromBasename(picked.basename);
    // Never show the same wallpaper twice in a row while rotation is active and
    // more than one image exists (a reseed can land the fresh lap on the
    // just-shown file). The cursor has already stepped past it.
    if (wallpaper_direct_pick::isImmediateRepeat(APP_STATE.wallpaperRotationPaused,
                                                 pick.fullPath == APP_STATE.lastSleepWallpaperPath, snap.count > 1)) {
      continue;
    }
    if (probe(pick)) {
      // Diagnostic for on-device confirmation (LOG_INF survives a release
      // build): count is the index size, pos the lap position just consumed.
      LOG_INF("SLP", "rot engine=index count=%u pos=%u pick=%s", static_cast<unsigned>(snap.count),
              static_cast<unsigned>(cursor.position), pick.basename.c_str());
      // Persist cursor + lastShown bookkeeping in ONE state.json flush (the
      // old path wrote state.json up to three times per lock).
      storeCursor(cursor);
      v2::WallpaperPlaylistV2::instance().rememberRendered(pick.fullPath, pick.basename);
      return pick;
    }
    // Probe rejected this candidate (decode/open failure). The cursor already
    // stepped past it; the next pickNext returns the following live file.
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
  windex::markDirty();
}

bool reshuffle() {
  ensureConfigured();
  if (APP_STATE.sleepIndexCount == 0 && countImages(2) == 0) return false;
  // Fresh lap: new shuffle seed, cursor rewound. The index itself is untouched
  // (a reshuffle changes the visit order, not the folder contents).
  sleep_rotation::Cursor c;
  sleep_rotation::reseed(c, APP_STATE.sleepIndexCount, static_cast<uint32_t>(esp_random()),
                         static_cast<uint32_t>(esp_random()));
  storeCursor(c);
  APP_STATE.saveToFile();
  return true;
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
      // Feed the watchdog / yield during a long move so a large batch never
      // trips the task WDT or fully stalls the UI task.
      if (moved % 16 == 0) {
        esp_task_wdt_reset();
        yield();
      }
    }
  }
  if (moved > 0) v2::WallpaperPlaylistV2::instance().markFolderDirty();
  return moved;
}

size_t countByFavorite(bool favorites, size_t scanCap) {
  ensureConfigured();
  const auto& deps = v2::WallpaperPlaylistV2::instance().deps();
  ISleepFs* sfs = deps.fs;
  if (!sfs) return 0;
  return countSleepImagesByFavorite(*sfs, deps.isFavorite, favorites, scanCap);
}

namespace {
// Bounded batches keep peak heap at kBatch names regardless of folder size;
// yield every kYieldEvery renames so a large sweep stays watchdog-safe. Fire the
// progress callback every kProgressStep moves so a long move shows a rising count
// (not too often — each repaint is a synchronous e-ink refresh).
constexpr size_t kMoveBatch = 128;
constexpr size_t kMoveYieldEvery = 16;
constexpr size_t kMoveProgressStep = 32;

// Wrap the reference-fixup callback with a running counter that also drives the
// UI progress callback. Returns an OnRenamedFn; `reported` must outlive the move.
OnRenamedFn makeCountingOnRenamed(const OnRenamedFn& base, const ProgressFn& onProgress, size_t& reported) {
  return [&base, &onProgress, &reported](const std::string& from, const std::string& to) {
    if (base) base(from, to);
    ++reported;
    if (onProgress && (reported % kMoveProgressStep == 0)) onProgress(reported);
  };
}
}  // namespace

size_t moveToPauseByFavorite(bool favorites, const ProgressFn& onProgress) {
  ensureConfigured();
  const auto& deps = v2::WallpaperPlaylistV2::instance().deps();
  ISleepFs* sfs = deps.fs;
  if (!sfs) return 0;

  size_t reported = 0;
  const OnRenamedFn onRenamed = makeCountingOnRenamed(deps.onPathRenamed, onProgress, reported);
  const size_t moved =
      moveSleepImagesByFavorite(*sfs, deps.isFavorite, onRenamed, favorites, kMoveBatch, kMoveYieldEvery, []() {
        esp_task_wdt_reset();
        yield();
      });
  if (moved > 0) v2::WallpaperPlaylistV2::instance().markFolderDirty();
  return moved;
}

size_t moveFavoritesToSleep(const ProgressFn& onProgress) {
  ensureConfigured();
  const auto& deps = v2::WallpaperPlaylistV2::instance().deps();
  ISleepFs* sfs = deps.fs;
  if (!sfs) return 0;

  size_t reported = 0;
  const OnRenamedFn onRenamed = makeCountingOnRenamed(deps.onPathRenamed, onProgress, reported);
  const size_t moved = moveSleepPauseImagesByFavorite(*sfs, deps.isFavorite, onRenamed, /*moveFavorites=*/true,
                                                      kMoveBatch, kMoveYieldEvery, []() {
                                                        esp_task_wdt_reset();
                                                        yield();
                                                      });
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
