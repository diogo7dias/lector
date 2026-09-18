#include "PartialUploads.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <vector>

namespace partial_uploads {
namespace {

constexpr const char* LOG_TAG = "PART";
constexpr size_t NAME_BUFFER_SIZE = 256;

}  // namespace

int sweepFolder(const std::string_view folder, const std::string_view keepPath) {
  const std::string directory = folder.empty() ? std::string("/") : std::string(folder);

  HalFile root = Storage.open(directory.c_str());
  if (!root || !root.isDirectory()) return 0;
  root.rewindDirectory();

  // One allocation for the whole walk: a name buffer per entry would fragment
  // DRAM across a folder with hundreds of books in it.
  auto nameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!nameBuffer) {
    LOG_ERR(LOG_TAG, "OOM: %u bytes", (unsigned)NAME_BUFFER_SIZE);
    return 0;
  }

  // The paths are collected first and removed after the walk: deleting an entry
  // while the directory handle is iterating it is not something SdFat promises
  // to survive.
  std::vector<std::string> doomed;
  for (HalFile entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    if (entry.isDirectory()) continue;
    entry.getName(nameBuffer.get(), NAME_BUFFER_SIZE);
    const std::string_view name{nameBuffer.get()};
    if (!FsHelpers::hasPartialExtension(name)) continue;

    std::string path = directory;
    if (path.back() != '/') path += '/';
    path += name;
    if (path == keepPath) continue;
    doomed.push_back(std::move(path));
  }
  root.close();

  int removed = 0;
  for (const std::string& path : doomed) {
    if (Storage.remove(path.c_str())) {
      removed++;
      LOG_DBG(LOG_TAG, "Removed the stale partial %s", path.c_str());
    }
  }
  return removed;
}

}  // namespace partial_uploads
