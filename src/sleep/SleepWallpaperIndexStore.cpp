#include "SleepWallpaperIndexStore.h"

#include <HalPowerManager.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_task_wdt.h>

#include <cstring>
#include <memory>

#include "../CrossPointState.h"
#include "SdFatSleepFs.h"

namespace crosspoint {
namespace sleep {
namespace windex {

namespace {

constexpr char kIndexDir[] = "/.crosspoint";
constexpr char kIndexPath[] = "/.crosspoint/sleep_index.bin";
constexpr char kIndexTmpPath[] = "/.crosspoint/sleep_index.tmp";
constexpr char kSleepDir[] = "/sleep";
// Directory entries processed per pumpIdle call. Each entry is one FAT read +
// (for wallpapers) one 160-byte tmp write — a few ms per call, so the scan
// never freezes input even on a 5000-file folder.
constexpr size_t kEntriesPerPump = 24;
constexpr size_t kWdtInterval = 50;

// Commutative per-entry hash (FNV-1a over name bytes + mtime), summed across
// the folder so the fingerprint is independent of FAT directory order.
uint32_t entryHash(const char* name, const size_t len, const uint32_t mtime) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < len; ++i) {
    h ^= static_cast<uint8_t>(name[i]);
    h *= 16777619u;
  }
  h ^= mtime;
  h *= 16777619u;
  return h;
}

enum class PumpPhase : uint8_t { Pending, Scanning, Done };

PumpPhase s_phase = PumpPhase::Pending;
bool s_dirty = false;
HalFile s_dir;
HalFile s_tmp;
uint32_t s_count = 0;
uint32_t s_fingerprint = 0;
size_t s_iter = 0;
bool s_failed = false;
// Held for the WHOLE idle scan (created at startScan, released at finish/abort).
// A per-tick Lock re-took and released the power lock every loop iteration,
// thrashing the CPU frequency and spamming "Lock already held" against the
// render task's own lock during refreshes.
std::unique_ptr<HalPowerManager::Lock> s_scanPowerLock;

// Append `name` as one fixed-size NUL-padded record. Returns false on SD error.
bool writeRecord(HalFile& out, const char* name, const size_t len) {
  char record[kRecordBytes];
  std::memset(record, 0, sizeof(record));
  std::memcpy(record, name, len);
  return out.write(record, sizeof(record)) == static_cast<int>(sizeof(record));
}

// Process one directory entry into (count, fingerprint, tmp records).
void acceptEntry(HalFile& file, const char* name, uint32_t& count, uint32_t& fingerprint, HalFile& tmp, bool& failed) {
  const size_t len = std::strlen(name);
  if (len == 0 || len >= kRecordBytes) {
    if (len >= kRecordBytes) LOG_ERR("WIDX", "Name too long for index (%u), skipped", static_cast<unsigned>(len));
    return;
  }
  if (count >= kMaxEntries) return;
  uint16_t fdate = 0, ftime = 0;
  file.getModifyDateTime(&fdate, &ftime);
  const uint32_t mtime = (static_cast<uint32_t>(fdate) << 16) | ftime;
  fingerprint += entryHash(name, len, mtime);
  if (!failed && !writeRecord(tmp, name, len)) failed = true;
  ++count;
}

void closeScanHandles() {
  if (s_dir) s_dir.close();
  if (s_tmp) s_tmp.close();
  s_scanPowerLock.reset();
}

// Rotate tmp -> live index and persist the new snapshot fields in RAM.
// `save` additionally flushes state.json (idle path saves; the lock path's
// caller batches the flush with the cursor save).
bool rotateIn(const uint32_t count, const uint32_t fingerprint, const bool save) {
  if (Storage.exists(kIndexPath)) Storage.remove(kIndexPath);
  if (!Storage.rename(kIndexTmpPath, kIndexPath)) {
    LOG_ERR("WIDX", "Index rotate failed");
    return false;
  }
  APP_STATE.sleepIndexCount = count;
  APP_STATE.sleepIndexFingerprint = fingerprint;
  if (save) APP_STATE.saveToFile();
  LOG_INF("WIDX", "Sleep index rebuilt: %u entries", static_cast<unsigned>(count));
  return true;
}

// Finish an idle scan: rotate in the fresh index only when the folder content
// actually changed (or the live index file is missing).
void finishIdleScan() {
  closeScanHandles();
  s_phase = PumpPhase::Done;
  if (s_failed) {
    Storage.remove(kIndexTmpPath);
    LOG_ERR("WIDX", "Idle index scan failed, keeping previous index");
    return;
  }
  const bool changed = s_count != APP_STATE.sleepIndexCount || s_fingerprint != APP_STATE.sleepIndexFingerprint ||
                       !Storage.exists(kIndexPath);
  if (changed) {
    rotateIn(s_count, s_fingerprint, /*save=*/true);
  } else {
    Storage.remove(kIndexTmpPath);
  }
}

bool startScan() {
  closeScanHandles();  // close-before-reopen: never reassign an open HalFile
  Storage.mkdir(kIndexDir);
  if (Storage.exists(kIndexTmpPath)) Storage.remove(kIndexTmpPath);
  s_dir = Storage.open(kSleepDir);
  if (!s_dir || !s_dir.isDirectory()) {
    if (s_dir) s_dir.close();
    // No /sleep folder at all: an empty snapshot is the correct index state.
    s_count = 0;
    s_fingerprint = 0;
    s_failed = false;
    if (APP_STATE.sleepIndexCount != 0) {
      APP_STATE.sleepIndexCount = 0;
      APP_STATE.sleepIndexFingerprint = 0;
      APP_STATE.saveToFile();
    }
    s_phase = PumpPhase::Done;
    return false;
  }
  if (!Storage.openFileForWrite("WIDX", kIndexTmpPath, s_tmp)) {
    s_dir.close();
    s_phase = PumpPhase::Done;
    return false;
  }
  s_count = 0;
  s_fingerprint = 0;
  s_iter = 0;
  s_failed = false;
  s_scanPowerLock = makeUniqueNoThrow<HalPowerManager::Lock>();  // full CPU speed for the scan's lifetime
  s_phase = PumpPhase::Scanning;
  return true;
}

}  // namespace

void markDirty() { s_dirty = true; }

bool isDirty() { return s_dirty; }

void pumpIdle() {
  if (s_dirty) {
    // Re-arm: abandon any in-flight scan so it restarts over fresh content.
    if (s_phase == PumpPhase::Scanning) {
      closeScanHandles();
      Storage.remove(kIndexTmpPath);
    }
    s_phase = PumpPhase::Pending;
    s_dirty = false;
  }
  if (s_phase == PumpPhase::Done) return;
  if (s_phase == PumpPhase::Pending && !startScan()) return;

  char name[256];
  for (size_t i = 0; i < kEntriesPerPump; ++i) {
    auto file = s_dir.openNextFile();
    if (!file) {
      finishIdleScan();
      return;
    }
    if (!file.isDirectory()) {
      file.getName(name, sizeof(name));
      if (isSleepImageName(name)) acceptEntry(file, name, s_count, s_fingerprint, s_tmp, s_failed);
    }
    file.close();
    if (++s_iter % kWdtInterval == 0) esp_task_wdt_reset();
  }
}

bool buildBlocking(Snapshot& out) {
  // Cancel any half-done idle scan; this full pass supersedes it.
  if (s_phase == PumpPhase::Scanning) closeScanHandles();
  s_phase = PumpPhase::Done;
  s_dirty = false;

  Storage.mkdir(kIndexDir);
  if (Storage.exists(kIndexTmpPath)) Storage.remove(kIndexTmpPath);
  auto dir = Storage.open(kSleepDir);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    out = Snapshot{};
    APP_STATE.sleepIndexCount = 0;
    APP_STATE.sleepIndexFingerprint = 0;
    return false;
  }
  HalFile tmp;
  if (!Storage.openFileForWrite("WIDX", kIndexTmpPath, tmp)) {
    dir.close();
    out = Snapshot{};
    return false;
  }

  uint32_t count = 0;
  uint32_t fingerprint = 0;
  size_t iter = 0;
  bool failed = false;
  char name[256];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      file.getName(name, sizeof(name));
      if (isSleepImageName(name)) acceptEntry(file, name, count, fingerprint, tmp, failed);
    }
    file.close();
    if (++iter % kWdtInterval == 0) {
      esp_task_wdt_reset();
      yield();
    }
  }
  dir.close();
  tmp.flush();
  tmp.close();
  if (failed) {
    Storage.remove(kIndexTmpPath);
    out = Snapshot{};
    return false;
  }
  if (!rotateIn(count, fingerprint, /*save=*/false)) {
    out = Snapshot{};
    return false;
  }
  out.count = count;
  out.fingerprint = fingerprint;
  return true;
}

bool Reader::open() {
  // Close-before-reopen: the rebuild path re-opens this Reader after a
  // buildBlocking, and reassigning an open HalFile is undefined (see the
  // DESTRUCTOR_CLOSES_FILE rules in CLAUDE.md).
  if (file_) file_.close();
  if (!Storage.exists(kIndexPath)) return false;
  return Storage.openFileForRead("WIDX", kIndexPath, file_);
}

std::string Reader::nameAt(const size_t index) {
  if (!file_) return {};
  char record[kRecordBytes];
  if (!file_.seek(index * kRecordBytes) || file_.read(record, sizeof(record)) != static_cast<int>(sizeof(record))) {
    return {};
  }
  record[kRecordBytes - 1] = '\0';
  if (record[0] == '\0' || std::memchr(record, '\0', kRecordBytes) == nullptr) return {};
  return std::string(record);
}

}  // namespace windex
}  // namespace sleep
}  // namespace crosspoint
