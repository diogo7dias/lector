/**
 * @file SleepWallpaperIndexStore.h
 * @brief Fixed-record SD index over /sleep for O(1) wallpaper rotation picks.
 *
 * Replaces the per-lock directory scans (2-4 full FAT walks + per-name exists()
 * probes = O(N²), ~13.5 s at 360 files on X3) with a card-catalog file:
 * /.crosspoint/sleep_index.bin holds one 160-byte NUL-terminated basename per
 * wallpaper. A lock-time pick is then seek(record) + read — independent of how
 * many thousands of images /sleep holds.
 *
 * The index is (re)built by a chunked idle pump (pumpIdle, called from loop()
 * while the user reads) so the scan never sits on the user-blocking sleep-entry
 * path; buildBlocking exists as the lock-time fallback for a first boot or a
 * dirty index. Rebuilds are gated on a content fingerprint (count + commutative
 * FNV over name+mtime) persisted in APP_STATE, so an unchanged folder costs one
 * verification scan per wake at idle and nothing at lock.
 *
 * Excluded from the host test build: HalStorage pulls in ESP32-only headers.
 * The pick/rotation logic layered on top lives in SleepIndexPickPolicy.h /
 * SleepRotationPolicy.h, which are host-tested.
 */
#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace crosspoint {
namespace sleep {
namespace windex {

// One record per wallpaper. 160 covers the long generated filenames in real
// folders (~105 chars observed); names that do not fit are skipped at build
// with a log line. 5000 images ≈ 780 KB on SD, never fully in RAM.
constexpr size_t kRecordBytes = 160;

// Practical ceiling, mirrors large_folder_index::MAX_INDEX_ENTRIES.
constexpr size_t kMaxEntries = 20000;

struct Snapshot {
  uint32_t count = 0;
  uint32_t fingerprint = 0;
};

// Full synchronous scan of /sleep -> fresh index file. WDT-yielded; blocks the
// caller for one directory walk. Lock-path fallback only — prefer pumpIdle.
// Returns false on SD failure (out.count is 0 then).
bool buildBlocking(Snapshot& out);

// Random access into the built index. Open once per pick session.
class Reader {
 public:
  bool open();
  // Basename at `index`, or empty on a read error / blank record.
  std::string nameAt(size_t index);

 private:
  HalFile file_;
};

// Mark the index stale (uploads, moves, deletes, favorite toggles, format
// switch). The next pumpIdle cycle — or, failing that, the next lock — rebuilds.
void markDirty();
bool isDirty();

// True once this wake's idle verification/scan has fully settled and no
// re-scan is pending. Gate for other idle SD workers (the wallpaper
// prestager) so they can never trigger a blocking index build.
bool idleComplete();

// Chunked idle rebuild. Call from loop() when the device has been idle a few
// seconds: each call advances the scan by a bounded number of directory
// entries (a few ms), so a 5000-file folder is indexed across idle ticks
// without ever freezing input. Once per wake it verifies the fingerprint and
// rotates in a fresh index only when the folder actually changed. Cheap no-op
// after the per-wake verification has completed (unless markDirty re-arms it).
void pumpIdle();

}  // namespace windex
}  // namespace sleep
}  // namespace crosspoint
