/**
 * @file SleepWallpaperIndexStore.h
 * @brief Fixed-record SD index over the sleep folder for O(1) wallpaper rotation.
 *
 * /.crosspoint/sleep_index.bin holds one 160-byte NUL-padded basename per
 * wallpaper, behind a 160-byte header slot (magic + version + folder id). A
 * lock-time pick is then seek(record) + read — independent of how many
 * thousands of images the folder holds. 5000 images ≈ 780 KB on SD, never in
 * RAM; the only RAM the rotation keeps is the scalar cursor in APP_STATE.
 *
 * The folder is walked ONLY when it looks changed. A battery lock on the
 * Xteink boards is a full power cut, so every unlock boots as a power-on
 * reset; the boot gate therefore asks folderTailMoved() — a millisecond
 * slot-tail probe — before paying the walk, and a clean unlock scans nothing.
 * When the walk does run, new files are appended at EOF as the "fresh" region
 * the pick serves next; a folder that drifted too far (bad header, folder
 * switch, dead-slot pileup) is rebuilt from scratch into a tmp file and
 * rotated in, so the live index is never torn.
 *
 * The record count is always derived from the file size, never persisted —
 * count drift between state.json and the file is structurally impossible.
 *
 * Excluded from the host test build: HalStorage pulls in ESP32-only headers.
 * The pick/rotation/reconcile decisions layered on top live in
 * SleepQueuePolicy.h / SleepRotationPolicy.h / SleepIndexReconcilePolicy.h,
 * which are host-tested.
 */
#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "SleepQueuePolicy.h"

class GfxRenderer;

namespace crosspoint {
namespace sleep {
namespace windex {

// One record per wallpaper. 160 covers the long generated filenames in real
// folders (~105 chars observed); names that do not fit are skipped at build
// with a log line. Record i lives at offset (i + 1) * kRecordBytes — slot 0 is
// the header.
constexpr size_t kRecordBytes = 160;

// Folder id persisted in the header and APP_STATE.sleepIndexDirId.
const char* dirPathForId(uint8_t dirId);
// 1 = /.sleep when it exists (preferred, mirrors the sleep screen's priority),
// else 0 = /sleep.
uint8_t resolveSleepDirId();

// Random access into the built index. Open once per pick session.
class Reader {
 public:
  // Opens and validates the header. False = absent or corrupt (both mean
  // "no index" — the next cold boot rebuilds).
  bool open();
  size_t recordCount() const { return count; }
  uint8_t dirId() const { return headerDirId; }
  // Basename at `index`, or empty on a read error / blank record.
  std::string nameAt(size_t index);

 private:
  HalFile file;
  size_t count = 0;
  uint8_t headerDirId = 0;
};

// Task-safe RAM mark: "the sleep folder was mutated this session". Callable
// from any task (background rename worker, web server handlers) — it never
// touches APP_STATE. A main-task point folds it in via markDirty().
void noteMutation();
bool hasPendingMutation();

// MAIN TASK ONLY: persist the dirty mark into APP_STATE.sleepIndexDirty so the
// next boot reconciles even when it is a silent restart. Saves state.json the
// first time the flag flips; no-op (and no SD write) once set.
void markDirty();

// markDirty(), but only when `path` sits inside a folder the wallpaper
// rotation reads or stages (/sleep, /.sleep, /sleep pause). For file-serving
// code (web upload/rename/delete) that touches arbitrary paths.
void markDirtyIfSleepPath(const char* path);

// MAIN TASK ONLY: a favorite toggle renamed a wallpaper in place, inside the
// indexed folder. Membership did not change, so the folder needs no walk — but
// the rename moved BOTH of the boot gate's markers: the name feeds the
// fingerprint, and SdFat creates the new directory entry before freeing the
// old one, which can push the folder's last live slot out. Left alone, the next
// unlock reads a wallpaper toggle as "files were added to the card" and pays a
// full folder scan that appends nothing. This re-anchors the snapshot in place
// instead: one file stat for the exact fingerprint delta (a FAT rename copies
// every field but the name, so the mtime and size are still the old entry's),
// plus a millisecond tail probe. The index record keeps the OLD name; the pick
// resolves it through favoriteCounterpart(), so the wallpaper keeps its place
// in the current lap and the record is refreshed at the next rebuild.
// No-op for any path outside the indexed folder. The caller owes the
// state.json save.
void noteFavoriteRename(const std::string& oldPath, const std::string& newPath);

// MAIN TASK ONLY, boot gate: cheap change probe, milliseconds and no UI. True
// when the sleep folder's last live directory slot (or the resolved folder
// itself) differs from the last reconcile's snapshot — FAT appends extend the
// slot tail, so card-added files move it. Exists because a battery lock on the
// Xteink boards is a full power cut: every unlock arrives as a power-on reset,
// and without this probe every unlock would pay the folder walk.
bool folderTailMoved();

// MAIN TASK ONLY, cold boot: one folder walk; a trusted index returns without
// writing anything; a changed folder appends the new names (they jump the
// queue) or rebuilds. Draws its own UI on `renderer`: a late busy banner for
// the check, a popup + progress bar once real indexing work starts.
void reconcileAtColdBoot(GfxRenderer& renderer);

// MAIN TASK ONLY, Settings action: fresh shuffled lap over everything (fresh
// queue folded in). Persists state. False when the index holds no wallpapers.
bool reshuffleNow();

// APP_STATE <-> policy state conversion (main task only — APP_STATE fields).
sleep_queue::QueueState loadQueueState();
void storeQueueState(const sleep_queue::QueueState& s);

}  // namespace windex
}  // namespace sleep
}  // namespace crosspoint
