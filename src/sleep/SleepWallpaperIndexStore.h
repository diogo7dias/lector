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
#include <cstring>
#include <string>
#include <string_view>

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
  // Sequential sweep over every record, `cb(std::string_view name)`. Reads in
  // multi-record chunks instead of one seek+read per record, so hashing a 5000
  // record index is a handful of block reads rather than 5000 of them. Blank
  // records are skipped. Returns false when the file is not open.
  template <typename NameFn>
  bool forEachName(NameFn&& cb) {
    if (!file || count == 0) return false;
    if (!file.seek(kRecordBytes)) return false;  // past the header slot
    char chunk[kReadChunkRecords * kRecordBytes];
    for (size_t done = 0; done < count;) {
      const size_t batch = (count - done) < kReadChunkRecords ? (count - done) : kReadChunkRecords;
      const int want = static_cast<int>(batch * kRecordBytes);
      if (file.read(chunk, want) != want) return false;
      for (size_t i = 0; i < batch; ++i) {
        char* record = chunk + i * kRecordBytes;
        record[kRecordBytes - 1] = '\0';
        if (record[0] != '\0') cb(std::string_view(record, std::strlen(record)));
      }
      done += batch;
    }
    return true;
  }

 private:
  // 8 records = 1280 bytes per read: an order of magnitude fewer reads than one
  // seek per record, without putting a 4 KB buffer on the boot task's stack.
  static constexpr size_t kReadChunkRecords = 8;

  HalFile file;
  size_t count = 0;
  uint8_t headerDirId = 0;
};

// Snapshot patch for a wallpaper about to be deleted from (or moved out of) the
// indexed folder. Built BEFORE the file disappears, because the delta needs the
// entry's mtime and size. `valid` is false for anything outside the indexed
// folder or unreadable — commit then does nothing.
struct PendingDeletion {
  bool valid = false;
  uint32_t entryHash = 0;
  std::string path;
};

// MAIN TASK ONLY. Stat the file and build its snapshot delta. Call immediately
// before removing or renaming the file away.
PendingDeletion planDeletion(const std::string& path);

// MAIN TASK ONLY. Apply a planned deletion after the remove/rename succeeded:
// the record stays in the index as a dead slot the pick skips, the snapshot is
// patched in place, and the folder is NOT marked dirty — so the next boot still
// matches and skips the walk. Saves state.json. A pileup of dead slots (or the
// end of the current lap) is what eventually triggers a compacting rebuild.
// Pass persist=false inside a delete loop and call finishDeletions() once at
// the end: the snapshot patch itself is cheap, but the tail re-probe and the
// state.json write are not worth paying per file.
void commitDeletion(const PendingDeletion& pending, bool persist = true);

// Re-anchor the boot gate's tail marker, re-check the dead-slot threshold, and
// save state. Only needed after commitDeletion(..., /*persist=*/false).
void finishDeletions();

// True when `path` is the delete commitDeletion() just accounted for in place
// (and consumes that mark). The shared reference-cleanup helper asks this so a
// delete already patched into the snapshot does not also mark the folder dirty
// and re-arm the folder walk the patch exists to avoid.
bool deletionWasAccounted(const std::string& path);

// MAIN TASK ONLY. A new wallpaper file was created in the indexed folder:
// append its record at end of file and patch the snapshot, no folder walk. The
// appended record lands in the fresh region, so it is served at the next lock,
// exactly as a walking reconcile would have done. Falls back to markDirty()
// when the index cannot be extended (absent, full, name too long). Saves
// state.json. `path` must already exist on disk.
void noteCreated(const std::string& path);

// MAIN TASK ONLY. A pick just completed a lap. Compaction of dead slots is
// deferred to exactly this moment: every wallpaper of the lap has now been
// shown once, so flag a rebuild for the next cold boot when holes exist.
void noteLapWrapped();

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
