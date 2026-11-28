#include "Sortes.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <LibraryFormat.h>
#include <LibraryIndexFile.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iterator>

#include "BookProgressFile.h"
#include "BusyTick.h"
#include "util/TaskWatchdog.h"

namespace sortes {
namespace {
constexpr char CLIX_INDEX_PATH[] = "/.crosspoint/lector-library.idx";

struct Scan {
  char path[1024] = "/";
  char name[768] = {};
  char chosen[1024] = {};
  size_t resume[32] = {};
  size_t depth = 0;
  uint32_t count = 0;
};

bool equalsNoCase(const std::string_view a, const std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

std::string stemOfPath(const std::string_view path) {
  const size_t slash = path.find_last_of('/');
  std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
  const size_t dot = name.find_last_of('.');
  if (dot != std::string_view::npos && dot > 0) name = name.substr(0, dot);
  return std::string(name);
}

// One sequential batch scan over CLX v2 records (4 x 32 B = 128 B on stack).
// Returns true if the CLX v2 index was open and scanned; false if absent/invalid.
bool drawFromClixIndex(const bool wantUnread, std::string& pathOut, std::string* titleOut, std::string* authorOut,
                       Random random, ScanResult& resultOut) {
  library::LibraryIndexFile index;
  if (!index.open(CLIX_INDEX_PATH)) return false;

  const uint16_t total = index.bookCount();
  constexpr size_t BATCH = 4;
  library::ClixRecord batch[BATCH];
  uint32_t count = 0;
  library::ClixRecord chosenRec{};
  std::string path;

  for (uint16_t first = 0; first < total; first += BATCH) {
    resetTaskWatchdogIfSubscribed();
    busy::tick();
    const size_t wanted = std::min<size_t>(BATCH, total - first);
    const size_t got = index.readRecords(first, wanted, batch);
    if (got < wanted) {
      resultOut = ScanResult::Failed;
      return true;
    }
    for (size_t i = 0; i < got; ++i) {
      const library::ClixRecord& r = batch[i];
      const bool eligible = wantUnread ? library::isRecordUnread(r) : library::isRecordRead(r);
      if (!eligible) continue;
      if (!index.readPath(r, path)) continue;
      const std::string_view pathSv(path);
      if (!wantUnread && !FsHelpers::hasEpubExtension(pathSv)) continue;
      if (wantUnread && !FsHelpers::hasEpubExtension(pathSv) && !FsHelpers::hasTextBookExtension(pathSv)) continue;
      if (!Storage.exists(path.c_str())) continue;
      if (below(++count, random) == 0) {
        chosenRec = r;
        pathOut = path;
      }
    }
  }

  if (count == 0) {
    pathOut.clear();
    resultOut = ScanResult::Empty;
    return true;
  }
  if (titleOut) {
    index.readTitle(chosenRec, *titleOut);
    if (titleOut->empty()) *titleOut = stemOfPath(pathOut);
  }
  if (authorOut) index.readAuthor(chosenRec, *authorOut);
  resultOut = ScanResult::Found;
  return true;
}

ScanResult walkLibrary(const bool wantUnread, std::string& selected, Random random) {
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
    if (std::strcmp(scan->name, ".") == 0 || std::strcmp(scan->name, "..") == 0 ||
        (scan->depth == 0 && std::strcmp(scan->name, ".crosspoint") == 0) || FsHelpers::isSystemFolderName(scan->name))
      continue;
    if (scan->depth == 0 && entry.isDirectory() && FsHelpers::isLibrarySkippedRootFolder(scan->name)) continue;
    const size_t parentLength = std::strlen(scan->path);
    const size_t prefix = parentLength == 1 && scan->path[0] == '/' ? 0 : parentLength;
    if (prefix + 1 + length >= sizeof(scan->path)) return ScanResult::Failed;
    scan->path[prefix] = '/';
    std::memcpy(scan->path + prefix + 1, scan->name, length + 1);
    if (entry.isDirectory()) {
      if (scan->depth == std::size(scan->resume)) return ScanResult::Failed;
      scan->resume[scan->depth++] = dir.position();
      dir = std::move(entry);
      continue;
    }
    const std::string_view sv(scan->name);
    const bool isSupported = wantUnread ? (FsHelpers::hasEpubExtension(sv) || FsHelpers::hasTextBookExtension(sv))
                                        : FsHelpers::hasEpubExtension(sv);
    if (isSupported) {
      book_progress::Marker marker;
      const bool hasMarker = book_progress::readForBook(scan->path, marker);
      if (wantUnread) {
        if (!hasMarker && below(++scan->count, random) == 0) std::strcpy(scan->chosen, scan->path);
      } else if (hasMarker && consider(marker.percent, scan->count, random)) {
        std::strcpy(scan->chosen, scan->path);
      }
    }
    scan->path[prefix] = '\0';
  }
  if (scan->count == 0) return ScanResult::Empty;
  selected = scan->chosen;
  return ScanResult::Found;
}
}  // namespace

ScanResult findBook(std::string& selected, Random random) {
  selected.clear();
  ScanResult idxResult = ScanResult::Empty;
  if (drawFromClixIndex(/*wantUnread=*/false, selected, nullptr, nullptr, random, idxResult)) {
    return idxResult;
  }
  return walkLibrary(/*wantUnread=*/false, selected, random);
}

ScanResult findUnreadBook(UnreadPick& pick, Random random) {
  pick = {};
  ScanResult idxResult = ScanResult::Empty;
  if (drawFromClixIndex(/*wantUnread=*/true, pick.path, &pick.title, &pick.author, random, idxResult)) {
    return idxResult;
  }
  const ScanResult res = walkLibrary(/*wantUnread=*/true, pick.path, random);
  if (res == ScanResult::Found) pick.title = stemOfPath(pick.path);
  return res;
}

bool findBookByTitle(const std::string_view title, std::string& pathOut) {
  pathOut.clear();
  if (title.empty()) return false;
  library::LibraryIndexFile index;
  if (!index.open(CLIX_INDEX_PATH)) return false;

  const uint16_t total = index.bookCount();
  constexpr size_t BATCH = 4;
  library::ClixRecord batch[BATCH];
  std::string recTitle;
  std::string path;

  for (uint16_t first = 0; first < total; first += BATCH) {
    const size_t wanted = std::min<size_t>(BATCH, total - first);
    const size_t got = index.readRecords(first, wanted, batch);
    if (got < wanted) break;
    for (size_t i = 0; i < got; ++i) {
      if (!index.readTitle(batch[i], recTitle)) continue;
      if (equalsNoCase(recTitle, title) && index.readPath(batch[i], path) && Storage.exists(path.c_str())) {
        pathOut = std::move(path);
        return true;
      }
    }
  }
  return false;
}

}  // namespace sortes
