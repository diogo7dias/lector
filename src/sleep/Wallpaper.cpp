#include "Wallpaper.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

#include <string>
#include <vector>

#include "../CrossPointState.h"
#include "../util/FavoriteImage.h"
#include "SdFatSleepFs.h"
#include "SleepFavoriteMove.h"
#include "SleepMoveSelection.h"
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

// Slack added on top of the measured sequential-playlist cost before deciding
// it's affordable. Covers small ancillary allocations during load/reconcile.
constexpr size_t kSeqGateHeadroom = 12 * 1024;

// Decide whether the sequential playlist (advance/reconcile/rebuild) can be
// materialized safely this cycle, or whether to fall back to the O(1)-heap
// direct pick. Measures the REAL cost with a streaming, zero-heap walk (sums
// filename bytes) and requires both enough contiguous heap (largest single
// block) and enough total free. Conservative on purpose: under-gating only
// costs a direct pick; over-gating costs a brick.
bool sequentialPlaylistAffordable() {
  const auto& deps = v2::WallpaperPlaylistV2::instance().deps();
  auto* sfs = deps.fs;
  if (!sfs) return false;
  size_t count = 0;
  size_t bufferBytes = 0;  // == order-buffer size: sum of (filename length + 1)
  sfs->walkSleepBmps([&](const char* /*name*/, size_t len, uint32_t /*mtime*/) {
    ++count;
    bufferBytes += len + 1;
  });
  if (count == 0) return false;  // empty folder — direct pick returns empty too

  const size_t entryVecBytes = count * sizeof(crosspoint::sleep::SleepBmpEntry);
  // Largest single contiguous allocation the path makes: the order buffer, or
  // (on rebuild) the entry vector — whichever is bigger.
  const size_t contigNeed = (bufferBytes > entryVecBytes ? bufferBytes : entryVecBytes) + kSeqGateHeadroom;
  // Worst-case coexisting transient peak: order buffer + safeRead's String +
  // std::string copy (~3x buffer) plus the entry vector and its per-name strings.
  const size_t totalNeed = bufferBytes * 3 + entryVecBytes * 2 + kSeqGateHeadroom;

  // Use the SAME largest-free-block source the V2 playlist's inner gates use
  // (deps.largestFreeBlockFn) so the sequential-vs-direct decision stays
  // consistent. Fall back to the raw heap_caps probe if unset.
  const size_t largestFree =
      deps.largestFreeBlockFn ? deps.largestFreeBlockFn() : heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);

  return largestFree >= contigNeed && heap_caps_get_free_size(MALLOC_CAP_DEFAULT) >= totalNeed;
}

// ── Streaming, O(1)-heap access to the saved order file (sleep_order.txt) ─────
// The buffer engine owns that file and writes the shuffled / newest-first order
// there when the heap can afford it. On the fragmented cold-sleep heap the buffer
// engine is gated off and the direct pick runs — these helpers let the direct
// pick HONOR the saved order WITHOUT ever loading the whole file. Every allocation
// here is bounded to one filename or a small fixed cap, so nothing can bad_alloc-
// abort the way the whole-buffer load did (the lock crash). Read buffers are on
// the stack; membership/next-after each stream the file one line at a time.

// Longest name we retain while streaming a line (SD long-name cap sits under this).
constexpr size_t kOrderLineMax = 280;
// Hard cap on files spliced to the front of the order file in one wake. Each is a
// short heap std::string; bounding it prevents the "every file is new" batch
// bad_alloc — the exact failure the buffer engine's kMaxNewFilesPerReconcile guards.
constexpr size_t kMaxNewOrderSplice = 8;

const std::string& orderFilePath() { return v2::WallpaperPlaylistV2::instance().deps().orderFilePath; }

// Stream the order file, invoking onName(const std::string&) per name line (the
// "v1 cursor=N" header line is skipped). onName returns true to stop early.
// O(1) heap: a 512-byte stack read buffer + one bounded line accumulator.
template <typename Fn>
bool streamOrderNames(Fn&& onName) {
  const std::string& path = orderFilePath();
  if (path.empty()) return false;
  HalFile f = Storage.open(path.c_str());
  if (!f || f.isDirectory()) return false;
  char buf[512];
  std::string line;
  line.reserve(kOrderLineMax + 8);  // bounded: pushes are capped at kOrderLineMax, never reallocs
  bool headerDone = false;
  bool stop = false;
  while (!stop) {
    const int n = f.read(buf, sizeof(buf));
    if (n <= 0) break;
    for (int i = 0; i < n; ++i) {
      const char c = buf[i];
      if (c == '\r') continue;
      if (c == '\n') {
        if (!headerDone) {
          headerDone = true;  // first line is the header — discard it
        } else if (!line.empty()) {
          if (onName(line)) {
            stop = true;
            break;
          }
        }
        line.clear();
      } else if (headerDone && line.size() < kOrderLineMax) {
        line.push_back(c);
      }
    }
  }
  if (!stop && headerDone && !line.empty()) onName(line);  // trailing name, no final newline
  return headerDone;
}

// Next name strictly after `after` in the saved order, wrapping to the first name
// (also when `after` is empty or absent). Empty when there is no usable order file.
std::string orderNextAfter(const std::string& after) {
  auto* sfs = v2::WallpaperPlaylistV2::instance().deps().fs;
  std::string firstName;
  std::string result;
  bool prevMatched = after.empty();
  streamOrderNames([&](const std::string& name) -> bool {
    // Skip names whose file has vanished (moved to pause / deleted) and keep
    // walking in order, so a stale order-file entry never strands the cursor.
    if (sfs && !sfs->exists("/sleep/" + name)) return false;
    if (firstName.empty()) firstName = name;
    if (prevMatched) {
      result = name;
      return true;
    }
    if (name == after) prevMatched = true;
    return false;
  });
  return result.empty() ? firstName : result;
}

size_t orderNameCount() {
  size_t count = 0;
  streamOrderNames([&count](const std::string&) -> bool {
    ++count;
    return false;
  });
  return count;
}

bool orderContains(const std::string& target) {
  bool found = false;
  streamOrderNames([&](const std::string& name) -> bool {
    if (name == target) {
      found = true;
      return true;
    }
    return false;
  });
  return found;
}

// Splice newly-added /sleep files to the FRONT of the saved order so fresh uploads
// show next. Cheap count-compare gate first; the membership scan and the atomic
// rewrite are all streaming (O(1) heap, bounded to kMaxNewOrderSplice names).
// Returns how many were spliced. Only meaningful when an order file already exists
// (a missing one is left for the buffer engine to build).
int prependNewOrderFiles() {
  const auto& deps = v2::WallpaperPlaylistV2::instance().deps();
  auto* sfs = deps.fs;
  auto* io = deps.fileIO;
  if (!sfs || !io) return 0;

  const size_t orderCount = orderNameCount();
  if (orderCount == 0) return 0;  // no saved order yet — leave building to the buffer engine

  size_t folderCount = 0;
  sfs->walkSleepBmps([&folderCount](const char*, size_t, uint32_t) { ++folderCount; });
  if (folderCount <= orderCount) return 0;  // nothing net-new since the order was written

  std::vector<std::string> newNames;
  newNames.reserve(kMaxNewOrderSplice);  // bounded: <= 8 short filenames, ~2 KB worst case
  sfs->walkSleepBmps([&](const char* name, size_t len, uint32_t) {
    if (newNames.size() >= kMaxNewOrderSplice || len == 0 || len > kOrderLineMax) return;
    std::string n(name, len);
    if (!orderContains(n)) newNames.push_back(std::move(n));
  });
  if (newNames.empty()) return 0;

  const std::string& path = orderFilePath();
  const bool ok = io->safeWriteStreamed(path, [&path, &newNames](persist::JsonSink& sink) -> bool {
    static constexpr char kHeader[] = "v1 cursor=0\n";
    sink.write(reinterpret_cast<const uint8_t*>(kHeader), sizeof(kHeader) - 1);
    for (const auto& n : newNames) {
      sink.write(reinterpret_cast<const uint8_t*>(n.data()), n.size());
      sink.write(static_cast<uint8_t>('\n'));
    }
    // Append the OLD order body (header stripped) after the new front entries.
    // safeWriteStreamed writes to <path>.tmp and only rotates AFTER this producer
    // returns, so `path` still holds the old content here.
    HalFile f = Storage.open(path.c_str());
    if (f && !f.isDirectory()) {
      char rbuf[512];
      bool headerSkipped = false;
      while (true) {
        const int n = f.read(rbuf, sizeof(rbuf));
        if (n <= 0) break;
        int start = 0;
        if (!headerSkipped) {
          for (int i = 0; i < n; ++i) {
            if (rbuf[i] == '\n') {
              start = i + 1;
              headerSkipped = true;
              break;
            }
          }
          if (!headerSkipped) continue;  // header spans past this chunk
        }
        if (start < n) sink.write(reinterpret_cast<const uint8_t*>(rbuf + start), static_cast<size_t>(n - start));
      }
    }
    return true;
  });
  return ok ? static_cast<int>(newNames.size()) : 0;
}

// Fragment-safe pick. Prefers the saved order (shuffle + newest-first), streamed
// one line at a time; falls back to the deterministic lexicographic walk only when
// no usable order file exists. `after` is the just-shown basename persisted across
// deep sleep, so successive wakes advance one entry at a time.
std::string pickDirectBasename(const std::string& after) {
  auto* sfs = v2::WallpaperPlaylistV2::instance().deps().fs;
  if (!sfs) return {};
  std::string ordered = orderNextAfter(after);
  const bool orderedExists = !ordered.empty() && sfs->exists("/sleep/" + ordered);
  if (wallpaper_direct_pick::source(!ordered.empty(), orderedExists) == wallpaper_direct_pick::Source::SavedOrder) {
    return ordered;
  }
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

  // Sequential-playlist gate: only run the buffer-backed advance()/reconcile()
  // when the heap can truly afford to materialize the order list for THIS
  // folder; otherwise fall back to the O(1)-heap, anti-repeat direct pick.
  const bool useDirectPick = !sequentialPlaylistAffordable();

  // Both engines share ONE cursor (lastShownSleepFilename) and walk the same
  // stable order — the buffer engine in RAM, the direct path via orderNextAfter
  // streaming — so the heap-gate engine flip resolves to the same successor
  // either way. This replaces the old split cursor (a separate
  // lastDirectPickFilename), whose divergence under the wake-to-wake engine flip
  // produced the two-image ping-pong.
  std::string directAfter = APP_STATE.lastShownSleepFilename;

  // When the low-heap direct pick is active, fold any newly-added /sleep files into
  // the FRONT of the saved order (streaming, bounded) so fresh uploads show next.
  // The buffer path handles new files itself via reconcile(), so only do it here.
  if (useDirectPick && prependNewOrderFiles() > 0) {
    directAfter.clear();  // start at the new front so the freshest file is shown next
  }

  // Whether more than one wallpaper exists. The immediate-repeat guard below only
  // skips a repeat when there is something else to show; countSleepBmps is O(1)
  // heap and the scan is capped at 2 (we only need "is there a second file?").
  const bool moreThanOneFile = [] {
    auto* fs = v2::WallpaperPlaylistV2::instance().deps().fs;
    return fs && fs->countSleepBmps(2) > 1;
  }();

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
    // Never show the same wallpaper twice in a row while rotation is active. The
    // buffer-backed and direct-pick engines keep separate cursors, so a low-memory
    // switch between them (or a reset direct cursor) can otherwise re-pick the
    // just-shown file. Skip it and advance to the next candidate. Paused rotation
    // and single-file folders are exempt (see isImmediateRepeat).
    if (wallpaper_direct_pick::isImmediateRepeat(APP_STATE.wallpaperRotationPaused,
                                                 pick.fullPath == APP_STATE.lastSleepWallpaperPath, moreThanOneFile)) {
      directAfter = basename;  // step the direct cursor past the repeat; the buffer
                               // path progresses on its own via the next advance()
      continue;
    }
    if (probe(pick)) {
      // Diagnostic for on-device confirmation (LOG_INF survives a release build):
      // cursorIn should equal the previous wake's pick, and pick should march
      // through every wallpaper regardless of which engine ran.
      LOG_INF("SLP", "rot engine=%s cursorIn=%s pick=%s lastShown=%s afford=%d", pickedDirect ? "direct" : "buffer",
              directAfter.c_str(), pick.basename.c_str(), APP_STATE.lastShownSleepFilename.c_str(),
              (int)!useDirectPick);
      // One shared cursor: rememberRendered() persists lastShownSleepFilename +
      // lastSleepWallpaperPath (state.json) when the shown pick changes. A pick is
      // always a forward successor now, so there is no unchanged-render freeze to
      // force-save around.
      v2::WallpaperPlaylistV2::instance().rememberRendered(pick.fullPath, pick.basename);
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

size_t moveToPauseByFavorite(bool favorites) {
  ensureConfigured();
  const auto& deps = v2::WallpaperPlaylistV2::instance().deps();
  ISleepFs* sfs = deps.fs;
  if (!sfs) return 0;

  // Bounded batches keep peak heap at kBatch names regardless of folder size;
  // yield every kYieldEvery renames so a large sweep stays watchdog-safe.
  constexpr size_t kBatch = 128;
  constexpr size_t kYieldEvery = 16;
  const size_t moved =
      moveSleepImagesByFavorite(*sfs, deps.isFavorite, deps.onPathRenamed, favorites, kBatch, kYieldEvery, []() {
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
