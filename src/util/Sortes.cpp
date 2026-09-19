#include "Sortes.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>
#include <iterator>

#include "BookProgressFile.h"
#include "BusyTick.h"
#include "util/TaskWatchdog.h"

namespace sortes {
namespace {
struct Scan {
  char path[1024] = "/";
  char name[768] = {};
  char chosen[1024] = {};
  size_t resume[32] = {};
  size_t depth = 0;
  uint32_t count = 0;
};
}  // namespace

ScanResult findBook(std::string& selected, Random random) {
  selected.clear();
  // One checked, bounded allocation: these path buffers exceed the C3's 256-byte
  // local budget. No list of books or recursive stack. HAL handles and the existing
  // readForBook string API allocate transiently; only the winning path is retained.
  auto scan = makeUniqueNoThrow<Scan>();
  if (!scan) {
    LOG_ERR("SORTES", "OOM: library scan");
    return ScanResult::Failed;
  }
  auto dir = Storage.open(scan->path);
  if (!dir || !dir.isDirectory()) return ScanResult::Failed;
  for (;;) {
    resetTaskWatchdogIfSubscribed();
    busy::tick();
    auto entry = dir.openNextFile();
    if (!entry) {
      if (scan->depth == 0) break;
      *std::strrchr(scan->path, '/') = '\0';
      dir = Storage.open(scan->path[0] ? scan->path : "/");
      if (!dir || !dir.isDirectory() || !dir.seekSet(scan->resume[--scan->depth])) {
        return ScanResult::Failed;
      }
      continue;
    }
    const size_t length = entry.getName(scan->name, sizeof(scan->name));
    if (length == 0 || length >= sizeof(scan->name) - 1) return ScanResult::Failed;
    // Never descend into generated caches. Hidden user folders remain eligible.
    if (std::strcmp(scan->name, ".") == 0 || std::strcmp(scan->name, "..") == 0 ||
        (scan->depth == 0 && std::strcmp(scan->name, ".crosspoint") == 0))
      continue;
    const size_t parentLength = std::strlen(scan->path);
    const size_t prefix = parentLength == 1 && scan->path[0] == '/' ? 0 : parentLength;
    if (prefix + 1 + length >= sizeof(scan->path)) return ScanResult::Failed;
    scan->path[prefix] = '/';
    std::memcpy(scan->path + prefix + 1, scan->name, length + 1);
    if (entry.isDirectory()) {
      // Refuse the whole draw on overflow; never select from a truncated library.
      if (scan->depth == std::size(scan->resume)) return ScanResult::Failed;
      scan->resume[scan->depth++] = dir.position();
      dir = std::move(entry);
      continue;
    }
    if (FsHelpers::hasEpubExtension(std::string_view(scan->name))) {
      book_progress::Marker marker;
      if (book_progress::readForBook(scan->path, marker) && consider(marker.percent, scan->count, random)) {
        std::strcpy(scan->chosen, scan->path);
      }
    }
    scan->path[prefix] = '\0';
  }
  if (scan->count == 0) return ScanResult::Empty;
  selected = scan->chosen;
  return ScanResult::Found;
}
}  // namespace sortes
