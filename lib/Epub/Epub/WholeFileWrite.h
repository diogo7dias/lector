#pragma once

#include <FsHelpers.h>
#include <HalStorage.h>

#include <string>

// Writes `path` through `path.part` and renames it into place only when `fill(HalFile&)`
// succeeds. The book cache trusts any file that exists, so a reset mid-write must never leave
// a cut file under the final name; a stale .part is never read and is truncated by the next
// write. FAT rename never overwrites, so an older file under the name goes only once the new
// one is whole.
template <class Fill>
bool writeWholeFile(const char* module, const std::string& path, Fill&& fill) {
  const std::string partPath = FsHelpers::partialPathFor(path);
  HalFile out;
  if (!Storage.openFileForWrite(module, partPath, out)) {
    return false;
  }
  bool ok = fill(out);
  out.flush();
  out.close();
  ok = ok && (!Storage.exists(path.c_str()) || Storage.remove(path.c_str())) &&
       Storage.rename(partPath.c_str(), path.c_str());
  if (!ok) {
    Storage.remove(partPath.c_str());
  }
  return ok;
}
