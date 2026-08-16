#include "SleepWallpaperIndexStore.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_random.h>

#include <cstring>
#include <string_view>

#include "CrossPointState.h"
#include "DirSlotProbe.h"
#include "SleepFolderPolicy.h"
#include "SleepIndexReconcilePolicy.h"
#include "WallpaperNames.h"
#include "components/BusyBanner.h"
#include "components/UITheme.h"
#include "util/BusyTick.h"
#include "util/FavoriteImageNames.h"
#include "util/TaskWatchdog.h"

namespace crosspoint {
namespace sleep {
namespace windex {

namespace {

constexpr char kIndexDir[] = "/.crosspoint";
constexpr char kIndexPath[] = "/.crosspoint/sleep_index.bin";
constexpr char kIndexTmpPath[] = "/.crosspoint/sleep_index.tmp";
// Header slot 0: magic + version + the folder id the records were scanned
// from. Anything else (missing file, wrong magic, future version) reads as "no
// index" and forces a rebuild — corruption can never limp along.
constexpr char kMagic[4] = {'L', 'W', 'X', '1'};
constexpr uint8_t kVersion = 1;
constexpr size_t kWdtInterval = 50;

// Set from any task by mutation sites; folded into APP_STATE by markDirty() on
// the main task only. Plain aligned bool: single writer semantics per site,
// torn reads impossible on a 32-bit store.
volatile bool s_pendingMutation = false;

struct Header {
  char magic[4];
  uint8_t version;
  uint8_t dirId;
};

void writeHeaderInto(char* record, const uint8_t dirId) {
  std::memcpy(record, kMagic, sizeof(kMagic));
  record[4] = static_cast<char>(kVersion);
  record[5] = static_cast<char>(dirId);
}

bool readHeader(HalFile& file, Header& out) {
  char record[kRecordBytes];
  if (!file.seek(0) || file.read(record, sizeof(record)) != static_cast<int>(sizeof(record))) return false;
  if (std::memcmp(record, kMagic, sizeof(kMagic)) != 0) return false;
  out.version = static_cast<uint8_t>(record[4]);
  out.dirId = static_cast<uint8_t>(record[5]);
  std::memcpy(out.magic, record, sizeof(out.magic));
  return out.version == kVersion;
}

// Append `name` as one fixed-size NUL-padded record. Returns false on SD error.
bool writeRecord(HalFile& out, const char* name, const size_t len) {
  char record[kRecordBytes];
  std::memset(record, 0, sizeof(record));
  std::memcpy(record, name, len);
  return out.write(record, sizeof(record)) == sizeof(record);
}

// Per-entry snapshot contribution: name + mtime + size, summed commutatively so
// the fingerprint is independent of FAT directory order.
uint32_t folderEntryHash(HalFile& file, const char* name, const size_t len) {
  uint16_t fdate = 0, ftime = 0;
  file.getModifyDateTime(&fdate, &ftime);
  const uint32_t mtime = (static_cast<uint32_t>(fdate) << 16) | ftime;
  return sleep_reconcile::entryHash(std::string_view(name, len), mtime, static_cast<uint32_t>(file.fileSize()));
}

// Throttled popup progress: each update is a real panel refresh, so repaint
// only every 5 points and at least 500 ms apart.
struct ProgressPopup {
  const GfxRenderer& renderer;
  Rect layout{};
  bool shown = false;
  int lastPct = -1;
  uint32_t lastMs = 0;

  explicit ProgressPopup(const GfxRenderer& r) : renderer(r) {}

  void show() {
    if (shown) return;
    layout = GUI.drawPopup(renderer, tr(STR_INDEXING_WALLPAPERS));
    shown = true;
  }

  void update(const uint32_t done, const uint32_t total) {
    if (!shown || total == 0) return;
    const int pct = static_cast<int>(static_cast<uint64_t>(done) * 100 / total);
    const uint32_t now = millis();
    if (lastPct >= 0 && (pct < lastPct + 5 || now - lastMs < 500)) return;
    lastPct = pct;
    lastMs = now;
    GUI.fillPopupProgress(renderer, layout, pct);
  }
};

// One pass over the sleep folder. `perEntry` sees every accepted wallpaper
// (open handle for mtime/size, name, length); count/fingerprint accumulate.
// Returns false when the folder cannot be opened (treated as empty upstream).
template <typename PerEntryFn>
bool walkFolder(const char* dirPath, uint32_t& count, uint32_t& fingerprint, ProgressPopup* progress,
                PerEntryFn&& perEntry) {
  auto dir = Storage.open(dirPath);
  if (!dir || !dir.isDirectory()) return false;
  const size_t slotCount = liveSlotCount(dir);  // progress denominator, ~2*log2(N) probes
  dir.rewindDirectory();

  char name[256];  // FAT long-file-name maximum
  size_t iter = 0;
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      file.getName(name, sizeof(name));
      const size_t len = std::strlen(name);
      if (len > 0 && len < kRecordBytes && isWallpaperName(std::string_view(name, len))) {
        fingerprint += folderEntryHash(file, name, len);
        ++count;
        perEntry(file, name, len);
      } else if (len >= kRecordBytes) {
        LOG_ERR("WIDX", "Name too long for index (%u), skipped", static_cast<unsigned>(len));
      }
    }
    file.close();
    if (++iter % kWdtInterval == 0) {
      resetTaskWatchdogIfSubscribed();
      vTaskDelay(1);
      busy::tick();
      if (progress != nullptr) {
        progress->update(static_cast<uint32_t>(dir.position()), static_cast<uint32_t>(slotCount * DIR_SLOT_BYTES));
      }
    }
  }
  return true;
}

// Offset (in slots) of the last live directory entry, 0 for missing/empty.
// ~2*log2(entries) probes — milliseconds, never a walk.
uint32_t probeTailSlot(const uint8_t dirId) {
  auto dir = Storage.open(dirPathForId(dirId));
  if (!dir || !dir.isDirectory()) return 0;
  return static_cast<uint32_t>(liveSlotCount(dir));
}

// The folder's own FAT modify date and time, packed into one uint32. 0 means
// missing folder or a driver that does not report one; see folderMarkersChanged
// for how that case is handled. One directory open, no probing at all.
uint32_t probeDirStamp(const uint8_t dirId) {
  auto dir = Storage.open(dirPathForId(dirId));
  if (!dir || !dir.isDirectory()) return 0;
  uint16_t fdate = 0, ftime = 0;
  if (!dir.getModifyDateTime(&fdate, &ftime)) return 0;
  return (static_cast<uint32_t>(fdate) << 16) | ftime;
}

// Re-anchor both boot-gate markers on the folder as it stands right now.
// Every in-place mutation ends with this, so a hooked change never leaves the
// gate thinking the card was touched from outside.
void stampFolderMarkers(const uint8_t dirId) {
  APP_STATE.sleepIndexTailSlot = probeTailSlot(dirId);
  APP_STATE.sleepIndexDirStamp = probeDirStamp(dirId);
}

// Reset the rotation to "fresh shuffled lap over `count`, nothing pending" and
// stamp the snapshot. Caller saves state.json (exactly once per reconcile).
void resetRotationState(const uint32_t count, const uint32_t fingerprint, const uint32_t liveCount,
                        const uint8_t dirId) {
  sleep_queue::QueueState qs;
  sleep_queue::reshuffle(qs, count, esp_random(), esp_random());
  storeQueueState(qs);
  APP_STATE.sleepIndexLiveCount = liveCount;
  APP_STATE.sleepIndexFingerprint = fingerprint;
  APP_STATE.sleepIndexDirId = dirId;
  APP_STATE.sleepIndexDirty = false;
  APP_STATE.sleepIndexNeedsRebuild = false;
  APP_STATE.sleepIndexDeadSlots = 0;  // a rebuild compacts every hole away
  stampFolderMarkers(dirId);
}

// Full scan -> tmp file -> rotate. The live index is replaced atomically; a
// crash mid-build leaves only an orphan tmp, deleted at the next build. The
// tmp's header is written first, but that is harmless: only a completed build
// ever rotates in.
bool buildBlocking(const uint8_t dirId, ProgressPopup* progress) {
  Storage.mkdir(kIndexDir);
  if (Storage.exists(kIndexTmpPath)) Storage.remove(kIndexTmpPath);

  HalFile tmp;
  if (!Storage.openFileForWrite("WIDX", kIndexTmpPath, tmp)) return false;
  {
    char header[kRecordBytes];
    std::memset(header, 0, sizeof(header));
    writeHeaderInto(header, dirId);
    if (tmp.write(header, sizeof(header)) != sizeof(header)) {
      tmp.close();
      Storage.remove(kIndexTmpPath);
      return false;
    }
  }

  uint32_t count = 0;
  uint32_t fingerprint = 0;
  bool failed = false;
  const bool opened =
      walkFolder(dirPathForId(dirId), count, fingerprint, progress, [&](HalFile&, const char* name, const size_t len) {
        if (failed) return;
        if (count > sleep_reconcile::kMaxEntries) return;  // count already incremented for this entry
        if (!writeRecord(tmp, name, len)) failed = true;
      });
  tmp.flush();
  tmp.close();
  if (failed) {
    Storage.remove(kIndexTmpPath);
    LOG_ERR("WIDX", "Index build failed, keeping previous index");
    return false;
  }
  // A missing folder builds an empty (header-only) index: the correct state.
  if (!opened) {
    count = 0;
    fingerprint = 0;
  }

  if (Storage.exists(kIndexPath)) Storage.remove(kIndexPath);
  if (!Storage.rename(kIndexTmpPath, kIndexPath)) {
    LOG_ERR("WIDX", "Index rotate failed");
    return false;
  }
  const uint32_t indexed = count > sleep_reconcile::kMaxEntries ? sleep_reconcile::kMaxEntries : count;
  resetRotationState(indexed, fingerprint, count, dirId);
  LOG_INF("WIDX", "Sleep index rebuilt: %u entries", static_cast<unsigned>(indexed));
  return true;
}

}  // namespace

const char* dirPathForId(const uint8_t dirId) { return dirId == sleep_folder::kHiddenDirId ? "/.sleep" : "/sleep"; }

uint8_t resolveSleepDirId() {
  // One slot probe per folder, never a walk: "is there a live entry at all?".
  const auto holdsEntries = [](const char* path) {
    auto dir = Storage.open(path);
    const bool populated = dir && dir.isDirectory() && entryExistsAt(dir, 0);
    if (dir) dir.close();
    return populated;
  };
  return sleep_folder::chooseDirId(holdsEntries("/sleep"), holdsEntries("/.sleep"));
}

bool Reader::open() {
  // Close-before-reopen: the rebuild path re-opens this Reader, and
  // reassigning an open HalFile is undefined (see DESTRUCTOR_CLOSES_FILE).
  if (file) file.close();
  count = 0;
  if (!Storage.exists(kIndexPath)) return false;
  if (!Storage.openFileForRead("WIDX", kIndexPath, file)) return false;
  Header header{};
  if (!readHeader(file, header)) {
    file.close();
    LOG_ERR("WIDX", "Bad index header, ignoring index");
    return false;
  }
  headerDirId = header.dirId;
  // Derived, never persisted: a torn trailing append simply does not count.
  const size_t bytes = file.fileSize();
  count = bytes >= kRecordBytes ? bytes / kRecordBytes - 1 : 0;
  return true;
}

std::string Reader::nameAt(const size_t index) {
  if (!file || index >= count) return {};
  char record[kRecordBytes];
  if (!file.seek((index + 1) * kRecordBytes) || file.read(record, sizeof(record)) != static_cast<int>(sizeof(record))) {
    return {};
  }
  record[kRecordBytes - 1] = '\0';
  if (record[0] == '\0') return {};
  return std::string(record);
}

namespace {

// Basename of `path` when it sits directly inside the indexed folder and is an
// entry the walk would have counted; empty otherwise.
std::string indexedNameIn(const std::string& path, const uint8_t dirId) {
  const std::string prefix = std::string(dirPathForId(dirId)) + "/";
  if (path.size() <= prefix.size() || path.compare(0, prefix.size(), prefix) != 0) return {};
  const std::string name = path.substr(prefix.size());
  if (name.find('/') != std::string::npos) return {};  // deeper path, different folder
  if (name.size() >= kRecordBytes || !isWallpaperName(name)) return {};
  return name;
}

sleep_reconcile::Snapshot snapshotFromState() {
  sleep_reconcile::Snapshot s;
  s.fingerprint = APP_STATE.sleepIndexFingerprint;
  s.liveCount = APP_STATE.sleepIndexLiveCount;
  s.deadSlots = APP_STATE.sleepIndexDeadSlots;
  return s;
}

void storeSnapshot(const sleep_reconcile::Snapshot& s) {
  APP_STATE.sleepIndexFingerprint = s.fingerprint;
  APP_STATE.sleepIndexLiveCount = s.liveCount;
  APP_STATE.sleepIndexDeadSlots = s.deadSlots;
}

// Records currently in the index file, 0 when there is no usable index.
uint32_t indexRecordCount() {
  Reader reader;
  if (!reader.open()) return 0;
  return static_cast<uint32_t>(reader.recordCount());
}

}  // namespace

PendingDeletion planDeletion(const std::string& path) {
  PendingDeletion pending;
  const uint8_t dirId = APP_STATE.sleepIndexDirId;
  const std::string name = indexedNameIn(path, dirId);
  if (name.empty()) return pending;
  // Only meaningful against a live index: with none, the next boot builds one.
  if (APP_STATE.sleepIndexLiveCount == 0) return pending;

  HalFile file;
  if (!Storage.openFileForRead("WIDX", path.c_str(), file)) return pending;
  pending.entryHash = folderEntryHash(file, name.c_str(), name.size());
  file.close();
  pending.path = path;
  pending.valid = true;
  return pending;
}

// Path of the delete the last commitDeletion() patched in, consumed by
// deletionWasAccounted(). Main task only, one delete at a time.
std::string& accountedDelete() {
  static std::string path;
  return path;
}

void commitDeletion(const PendingDeletion& pending, const bool persist) {
  if (!pending.valid) return;
  accountedDelete() = pending.path;
  storeSnapshot(sleep_reconcile::applyDeletion(snapshotFromState(), pending.entryHash));
  if (persist) finishDeletions();
}

void finishDeletions() {
  // The removed directory entries can free the folder's last live slot and they
  // restamp the folder, so the boot gate's markers have to move with them or the
  // next unlock pays a walk that would find nothing but the holes already
  // accounted for.
  stampFolderMarkers(APP_STATE.sleepIndexDirId);
  if (sleep_reconcile::deadSlotsDemandRebuild(indexRecordCount(), APP_STATE.sleepIndexDeadSlots)) {
    APP_STATE.sleepIndexNeedsRebuild = true;
  }
  APP_STATE.saveToFile();
}

bool deletionWasAccounted(const std::string& path) {
  if (accountedDelete().empty() || accountedDelete() != path) return false;
  accountedDelete().clear();
  return true;
}

void noteCreated(const std::string& path) {
  const uint8_t dirId = APP_STATE.sleepIndexDirId;
  const std::string name = indexedNameIn(path, dirId);
  if (name.empty()) {
    markDirtyIfSleepPath(path.c_str());
    return;
  }

  HalFile file;
  if (!Storage.openFileForRead("WIDX", path.c_str(), file)) {
    markDirty();
    return;
  }
  const uint32_t hash = folderEntryHash(file, name.c_str(), name.size());
  file.close();

  // Append at a record-aligned end of file: a torn tail from an earlier crash
  // is overwritten rather than extended.
  const uint32_t recordCount = indexRecordCount();
  if (recordCount == 0 || recordCount >= sleep_reconcile::kMaxEntries) {
    markDirty();  // no index yet, or full: let the cold-boot path decide
    return;
  }
  bool appended = false;
  HalFile idx = Storage.open(kIndexPath, O_RDWR);
  if (idx) {
    if (idx.seek((static_cast<size_t>(recordCount) + 1) * kRecordBytes) &&
        writeRecord(idx, name.c_str(), name.size())) {
      idx.flush();
      appended = true;
    }
    idx.close();
  }
  if (!appended) {
    markDirty();
    return;
  }

  storeSnapshot(sleep_reconcile::applyAddition(snapshotFromState(), hash));
  stampFolderMarkers(dirId);
  APP_STATE.saveToFile();
}

void noteLapWrapped() {
  if (APP_STATE.sleepIndexDeadSlots == 0 || APP_STATE.sleepIndexNeedsRebuild) return;
  // Lap over: every wallpaper has been shown once, so compacting the holes now
  // costs the user nothing in rotation fairness.
  APP_STATE.sleepIndexNeedsRebuild = true;
}

void noteMutation() { s_pendingMutation = true; }

bool hasPendingMutation() { return s_pendingMutation; }

void markDirty() {
  s_pendingMutation = false;
  if (APP_STATE.sleepIndexDirty) return;  // already persisted: no SD write
  APP_STATE.sleepIndexDirty = true;
  APP_STATE.saveToFile();
}

void markDirtyIfSleepPath(const char* path) {
  if (path == nullptr) return;
  const std::string_view p(path);
  const auto under = [&](const std::string_view prefix) {
    return p.size() > prefix.size() && p.substr(0, prefix.size()) == prefix;
  };
  // "/sleep pause" is staged, not indexed, but a web move out of it lands in
  // /sleep next restore — cheap to reconcile, wrong to miss.
  if (under("/sleep/") || under("/.sleep/") || under("/sleep pause/")) markDirty();
}

void noteFavoriteRename(const std::string& oldPath, const std::string& newPath) {
  // Only the folder the records were scanned from. A rename anywhere else
  // (a pause-folder move, a book cover) leaves this snapshot alone, and those
  // paths mark dirty themselves.
  const uint8_t dirId = APP_STATE.sleepIndexDirId;
  const std::string prefix = std::string(dirPathForId(dirId)) + "/";
  const auto nameIn = [&prefix](const std::string& path) -> std::string {
    if (path.size() <= prefix.size() || path.compare(0, prefix.size(), prefix) != 0) return {};
    std::string name = path.substr(prefix.size());
    // A deeper path is a different folder, not a name in this one.
    return name.find('/') == std::string::npos ? name : std::string{};
  };

  const std::string oldName = nameIn(oldPath);
  const std::string newName = nameIn(newPath);
  if (oldName.empty() || newName.empty() || oldName == newName) return;
  // Both sides must be entries the walk would have counted, or the delta would
  // describe a fingerprint the folder never had.
  if (!isWallpaperName(oldName) || !isWallpaperName(newName)) return;
  if (oldName.size() >= kRecordBytes || newName.size() >= kRecordBytes) return;

  HalFile file;
  if (!Storage.openFileForRead("WIDX", newPath.c_str(), file)) return;
  const uint32_t after = folderEntryHash(file, newName.c_str(), newName.size());
  const uint32_t before = folderEntryHash(file, oldName.c_str(), oldName.size());
  file.close();

  // Commutative wrap-sum: swapping one entry's contribution is exact.
  APP_STATE.sleepIndexFingerprint += after - before;
  stampFolderMarkers(dirId);
}

bool indexedFolderChanged() { return resolveSleepDirId() != APP_STATE.sleepIndexDirId; }

bool folderLooksChanged() {
  const uint8_t dirId = resolveSleepDirId();
  if (dirId != APP_STATE.sleepIndexDirId) return true;
  return sleep_folder::folderMarkersChanged(probeTailSlot(dirId), probeDirStamp(dirId), APP_STATE.sleepIndexTailSlot,
                                            APP_STATE.sleepIndexDirStamp);
}

sleep_queue::QueueState loadQueueState() {
  sleep_queue::QueueState s;
  s.cursor.position = APP_STATE.sleepCursorPos;
  s.cursor.multiplier = APP_STATE.sleepCursorMult;
  s.cursor.offset = APP_STATE.sleepCursorOff;
  s.cursor.seededCount = APP_STATE.sleepCursorSeededCount;
  s.cursor.seeded = APP_STATE.sleepCursorSeeded;
  s.freshNext = APP_STATE.sleepFreshNext;
  return s;
}

void storeQueueState(const sleep_queue::QueueState& s) {
  APP_STATE.sleepCursorPos = s.cursor.position;
  APP_STATE.sleepCursorMult = s.cursor.multiplier;
  APP_STATE.sleepCursorOff = s.cursor.offset;
  APP_STATE.sleepCursorSeededCount = s.cursor.seededCount;
  APP_STATE.sleepCursorSeeded = s.cursor.seeded;
  APP_STATE.sleepFreshNext = s.freshNext;
}

void reconcileAtColdBoot(GfxRenderer& renderer) {
  if (s_pendingMutation) markDirty();  // fold a pre-boot RAM mark (defensive)
  const uint8_t dirId = resolveSleepDirId();

  // Late banner — an unchanged folder's check paints nothing unless it
  // genuinely drags past the banner delay.
  BusyBanner banner(renderer, tr(STR_CHECKING_WALLPAPERS));
  uint32_t liveCount = 0;
  uint32_t fingerprint = 0;

  sleep_reconcile::DecideInput in;
  in.dirty = APP_STATE.sleepIndexDirty;
  in.needsRebuildFlag = APP_STATE.sleepIndexNeedsRebuild;
  in.snapLive = APP_STATE.sleepIndexLiveCount;
  in.snapFingerprint = APP_STATE.sleepIndexFingerprint;
  // Pre-walk: the folder has not been counted yet, so seed the scan halves with
  // the snapshot. Every rule that fires here (unusable index, folder switch,
  // rebuild flag, empty index, incremental cap) is decided by flags and record
  // counts alone; the scan-dependent rules only matter after the walk below.
  in.scannedLive = in.snapLive;
  in.scannedFingerprint = in.snapFingerprint;

  uint32_t recordCount = 0;
  {
    Reader reader;
    in.indexUsable = reader.open();
    in.dirChanged = in.indexUsable && reader.dirId() != dirId;
    recordCount = static_cast<uint32_t>(reader.recordCount());
    in.recordCount = recordCount;
  }

  auto plan = sleep_reconcile::decidePlan(in);
  // Reached only via the boot gate's tail probe, which already said something
  // moved: the pre-walk verdict can be NoChange, but the walk below is what
  // proves it. Fall into the append path and let it decide on real numbers.
  if (plan == sleep_reconcile::Plan::NoChange) plan = sleep_reconcile::Plan::IncrementalAppend;

  // Shown lazily: a rebuild is always real work, but an append pass that turns
  // out to find nothing new (a dirty mark from a rename or a delete) stays
  // under the checking banner instead of flashing "Indexing wallpapers".
  ProgressPopup progress(renderer);
  if (plan == sleep_reconcile::Plan::FullRebuild) progress.show();

  if (plan == sleep_reconcile::Plan::IncrementalAppend) {
    // Pass 1: hash the known records (transient, freed before boot continues).
    // Read sequentially in record chunks — one block read per 25 records, not
    // a seek per record.
    sleep_reconcile::NameHashSet known;
    known.reserve(recordCount);
    {
      Reader reader;
      if (reader.open()) {
        size_t seen = 0;
        reader.forEachName([&](const std::string_view name) {
          known.add(sleep_reconcile::nameHash(name));
          if (++seen % kWdtInterval == 0) {
            resetTaskWatchdogIfSubscribed();
            vTaskDelay(1);
          }
        });
      }
    }
    known.finalize();

    // Pass 2: the ONE folder walk. It counts, fingerprints, and appends the
    // unknown names (unknown under their favorite-rename counterpart too — a
    // toggle keeps its record) at EOF as the fresh region, all in a single
    // sweep. Seek record-aligned so a torn tail from an earlier crash is
    // overwritten, not extended.
    uint32_t appends = 0;
    bool appendFailed = false;
    liveCount = 0;
    fingerprint = 0;
    HalFile idx = Storage.open(kIndexPath, O_RDWR);
    if (idx && idx.seek((static_cast<size_t>(recordCount) + 1) * kRecordBytes)) {
      walkFolder(dirPathForId(dirId), liveCount, fingerprint, &progress,
                 [&](HalFile&, const char* name, const size_t len) {
                   if (appendFailed) return;
                   const std::string_view nameView(name, len);
                   if (known.contains(sleep_reconcile::nameHash(nameView))) return;
                   const std::string alt = FavoriteImage::favoriteCounterpart(nameView);
                   if (!alt.empty() && known.contains(sleep_reconcile::nameHash(alt))) return;
                   if (recordCount + appends >= sleep_reconcile::kMaxEntries) return;
                   progress.show();  // first real append: now it is indexing
                   if (!writeRecord(idx, name, len)) appendFailed = true;
                   ++appends;
                 });
      idx.flush();
    } else {
      appendFailed = true;
    }
    if (idx) idx.close();

    in.scannedLive = liveCount;
    in.scannedFingerprint = fingerprint;
    in.pendingAppends = appends;
    if (appendFailed || sleep_reconcile::decidePlan(in) == sleep_reconcile::Plan::FullRebuild) {
      plan = sleep_reconcile::Plan::FullRebuild;
    } else {
      // Finalize the incremental path: snapshot updated, appended records are
      // the fresh region ([old recordCount, new recordCount) — freshNext
      // already points at or before its start).
      const uint32_t newCount = recordCount + appends;
      // The walk just measured the truth, so the deferred-delete counter is
      // re-derived from it rather than carried forward.
      const uint32_t deadSlots = newCount > liveCount ? newCount - liveCount : 0;
      const uint32_t tailNow = probeTailSlot(dirId);
      const uint32_t stampNow = probeDirStamp(dirId);
      const uint32_t freshNext = APP_STATE.sleepFreshNext > newCount ? newCount : APP_STATE.sleepFreshNext;
      // A walk that found the folder exactly as the snapshot describes it (the
      // boot gate fired on a relocated directory entry) must not spend an SD
      // write to restate what is already on disk.
      const bool changed = liveCount != APP_STATE.sleepIndexLiveCount ||
                           fingerprint != APP_STATE.sleepIndexFingerprint || dirId != APP_STATE.sleepIndexDirId ||
                           APP_STATE.sleepIndexDirty || deadSlots != APP_STATE.sleepIndexDeadSlots ||
                           tailNow != APP_STATE.sleepIndexTailSlot || stampNow != APP_STATE.sleepIndexDirStamp ||
                           freshNext != APP_STATE.sleepFreshNext;
      APP_STATE.sleepIndexLiveCount = liveCount;
      APP_STATE.sleepIndexFingerprint = fingerprint;
      APP_STATE.sleepIndexDirId = dirId;
      APP_STATE.sleepIndexDirty = false;
      APP_STATE.sleepIndexTailSlot = tailNow;
      APP_STATE.sleepIndexDirStamp = stampNow;
      APP_STATE.sleepIndexDeadSlots = deadSlots;
      APP_STATE.sleepFreshNext = freshNext;
      if (changed) APP_STATE.saveToFile();
      LOG_INF("WIDX", "Sleep index reconciled: +%u of %u live", static_cast<unsigned>(appends),
              static_cast<unsigned>(liveCount));
      return;
    }
  }

  progress.show();  // idempotent; covers the incremental-to-rebuild fallthrough
  if (!buildBlocking(dirId, &progress)) {
    // Build failed (SD error): leave flags so the next cold boot retries, and
    // the sleep pick falls back to the jump pick meanwhile.
    APP_STATE.sleepIndexNeedsRebuild = true;
  }
  APP_STATE.saveToFile();
}

bool reshuffleNow() {
  Reader reader;
  if (!reader.open() || reader.recordCount() == 0) return false;
  auto qs = loadQueueState();
  sleep_queue::reshuffle(qs, reader.recordCount(), esp_random(), esp_random());
  storeQueueState(qs);
  APP_STATE.saveToFile();
  return true;
}

}  // namespace windex
}  // namespace sleep
}  // namespace crosspoint
