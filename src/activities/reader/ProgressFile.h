#pragma once

#include <HalStorage.h>
#include <Logging.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace ProgressFile {

// Writes `len` bytes of reader progress to `<cachePath>/progress.bin` without
// ever leaving the canonical file half-written.
//
// The bytes go to a temporary `progress.bin.tmp` first; only once that is fully
// written and closed is it renamed over progress.bin. An interrupted write
// (power loss or a crash mid-SPI) therefore damages only the throwaway temp file.
// Previously a truncate-in-place write that was cut short left progress.bin with
// a broken FAT cluster chain that the firmware could neither rewrite nor clear,
// stranding the book on an old page (issue #2275).
//
// This is crash-safe, not metadata-atomic: on FAT the replace is remove + rename,
// two separate directory operations, so a crash between them can leave neither
// file but the finished temp, which openForRead below renames into place, so the
// reader's place survives. The point is that progress.bin is never torn.
//
// Note: this prevents corruption on a healthy card going forward. It cannot
// repair an already-corrupted progress.bin -- removing the stale file may itself
// fail at the FAT level, in which case recovery still requires fsck on a host.
//
// Returns true only if the new progress.bin is fully in place.
//
// `name` lets other small per-book records (the library's percent badge) use the same
// write; the default is the reader's progress.bin.
inline bool writeAtomic(const std::string& cachePath, const uint8_t* data, size_t len,
                        const char* name = "/progress.bin") {
  const std::string finalPath = cachePath + name;
  const std::string tmpPath = finalPath + ".tmp";

  {
    HalFile f;
    if (!Storage.openFileForWrite("PRG", tmpPath, f)) {
      LOG_ERR("PRG", "Could not open temp progress file for write: %s", tmpPath.c_str());
      return false;
    }
    const size_t written = f.write(data, len);
    if (written != len) {
      LOG_ERR("PRG", "Short write saving progress to %s: %u/%u bytes", tmpPath.c_str(), (unsigned)written,
              (unsigned)len);
      return false;
    }
    // Closed here, not at scope exit: close() is where the last sector reaches the card,
    // and a failed one must not let this temp replace the saved progress. SdFat must not
    // rename a path that still has an open FsFile either.
    if (!f.close()) {
      LOG_ERR("PRG", "Failed to close temp progress %s", tmpPath.c_str());
      return false;
    }
  }

  // SdFat's rename does not overwrite an existing destination, so drop the old
  // canonical file first. The brief window where neither file exists reads as
  // "no saved progress" on next launch -- never a corrupt, unclearable file.
  Storage.remove(finalPath.c_str());
  if (Storage.rename(tmpPath.c_str(), finalPath.c_str())) return true;

  LOG_ERR("PRG", "Failed to rename temp progress into place: %s", finalPath.c_str());
  // The old progress.bin is already gone, so leaving it here would lose the
  // reader's place. Nothing is torn by writing the same bytes straight to the
  // canonical path now: either they land whole, or there was no saved progress
  // either way.
  HalFile f;
  if (!Storage.openFileForWrite("PRG", finalPath, f)) {
    LOG_ERR("PRG", "Could not write progress directly either: %s", finalPath.c_str());
    return false;
  }
  if (f.write(data, len) != len) {
    LOG_ERR("PRG", "Short direct write saving progress to %s", finalPath.c_str());
    return false;
  }
  f.flush();
  return true;
}

// Opens `<cachePath>/progress.bin` for reading. A power cut between writeAtomic's
// remove and rename leaves only the temp, which was fully written and closed
// before the remove, so it is renamed into place first instead of reading as
// "no saved progress".
inline bool openForRead(const char* tag, const std::string& cachePath, HalFile& f) {
  const std::string finalPath = cachePath + "/progress.bin";
  const std::string tmpPath = finalPath + ".tmp";
  if (!Storage.exists(finalPath.c_str()) && Storage.exists(tmpPath.c_str())) {
    LOG_INF("PRG", "Recovering progress from %s", tmpPath.c_str());
    Storage.rename(tmpPath.c_str(), finalPath.c_str());
  }
  return Storage.openFileForRead(tag, finalPath, f);
}

}  // namespace ProgressFile
