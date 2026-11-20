#include "LibraryBuilder.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iterator>

#include "LibraryIndexFile.h"

namespace library {
namespace {

constexpr char INDEX_PATH[] = "/.crosspoint/lector-library.idx";
constexpr char STAGE_PATH[] = "/.crosspoint/lector-library.stage";
constexpr char CACHE_DIR[] = "/.crosspoint";

// One staged book: its record as far as the walk can fill it, then its text, in a
// fixed stride so the emit passes are a seek to ordinal * 512, one sector.
struct StagedEntry {
  ClixRecord record;
  char title[CLIX_MAX_TITLE];
  char author[CLIX_MAX_AUTHOR];
  char path[CLIX_MAX_PATH + 1];
};
static_assert(sizeof(StagedEntry) == 512, "a staged entry is one sector");
constexpr size_t STAGE_STRIDE = sizeof(StagedEntry);

// Key to find a book of the previous index again. mtime is checked on the record.
struct PriorEntry {
  uint64_t pathHash;
  uint32_t fileSize;
  uint16_t ordinal;
  uint16_t reserved;
};
static_assert(sizeof(PriorEntry) == 16, "prior keys are the walk's main resident cost");

constexpr size_t SORT_PREFIX = 16;
struct SortKey {
  char prefix[SORT_PREFIX];
  uint16_t ordinal;
};

bool sortKeyLess(const SortKey& a, const SortKey& b) {
  const int c = std::memcmp(a.prefix, b.prefix, SORT_PREFIX);
  return c != 0 ? c < 0 : a.ordinal < b.ordinal;
}

struct Scan {
  char path[CLIX_MAX_PATH + 1 + 768] = "/";  // room to see a too-long path rather than overflow
  char name[768] = {};
  size_t resume[LIBRARY_MAX_DEPTH + 1] = {};
  int depth = 0;
};

struct Build {
  const char* indexPath;
  BuildStats* stats;
  ProgressFn progress;
  void* progressCtx;
  uint32_t entries = 0;

  LibraryIndexFile previous;
  std::unique_ptr<PriorEntry[]> prior;
  size_t priorCount = 0;
  std::unique_ptr<uint64_t[]> seen;
  size_t seenCount = 0;
  size_t seenCap = 0;

  HalFile stage;
  std::unique_ptr<StagedEntry> entry;
  ClixRecord batch[16];  // loading the previous index: one sector of records
  uint32_t nameBytes = 0;
  bool useMetadata = true;
  bool stop = false;
  bool failed = false;
};

void tick(Build& b) {
  if ((++b.entries & 0x0F) == 0 && b.progress) b.progress(b.progressCtx);
}

bool isQuotesSidecar(const std::string_view name) {
  constexpr std::string_view suffix = "_QUOTES.txt";
  return name.size() > suffix.size() && name.substr(name.size() - suffix.size()) == suffix;
}

// The name without its last extension: the title of a book that tells us nothing.
std::string stemOf(const std::string_view path) {
  const size_t slash = path.find_last_of('/');
  std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
  const size_t dot = name.find_last_of('.');
  if (dot != std::string_view::npos && dot > 0) name = name.substr(0, dot);
  return std::string(name);
}

bool equalsNoCase(const std::string_view a, const std::string_view b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const char x, const char y) {
           return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
         });
}

// Exporters write "Unknown" into dc:creator often enough that listing it would put a
// fictional author on a row.
bool isUnknownAuthor(const std::string_view author) { return equalsNoCase(author, "unknown"); }

void copyField(char* dst, const size_t cap, const std::string& text, uint8_t& lenOut) {
  const size_t n = std::min(text.size(), cap);
  std::memcpy(dst, text.data(), n);
  lenOut = static_cast<uint8_t>(n);
}

uint32_t packModified(HalFile& file) {
  uint16_t date = 0;
  uint16_t time = 0;
  if (!file.getModifyDateTime(&date, &time)) return 0;
  return (static_cast<uint32_t>(date) << 16) | time;
}

bool loadPrior(Build& b) {
  if (!b.previous.open(b.indexPath) || b.previous.bookCount() == 0) return true;
  if (b.previous.metadataEnabled() != b.useMetadata) {
    b.previous.close();
    return true;
  }
  const size_t count = b.previous.bookCount();
  b.prior = makeUniqueNoThrow<PriorEntry[]>(count);
  if (!b.prior) {
    LOG_ERR("LIBIDX", "OOM: %u prior keys; every book is parsed again", static_cast<unsigned>(count));
    b.previous.close();
    return true;  // slower, still right
  }
  constexpr size_t BATCH = 16;
  for (size_t at = 0; at < count; at += BATCH) {
    const size_t got = b.previous.readRecords(static_cast<uint16_t>(at), BATCH, b.batch);
    if (got != std::min(BATCH, count - at)) {
      LOG_ERR("LIBIDX", "previous index unreadable at %u; every book is parsed again", static_cast<unsigned>(at));
      b.prior.reset();
      b.previous.close();
      return true;
    }
    for (size_t i = 0; i < got; i++) {
      b.prior[at + i] = PriorEntry{b.batch[i].pathHash, b.batch[i].fileSize, static_cast<uint16_t>(at + i), 0};
    }
  }
  b.priorCount = count;
  std::sort(b.prior.get(), b.prior.get() + count,
            [](const PriorEntry& x, const PriorEntry& y) { return x.pathHash < y.pathHash; });
  return true;
}

// Title and author the previous index held for this exact file (same path, size, modified), if any.
bool reuseFromPrevious(Build& b, const uint64_t hash, const uint32_t size, const uint32_t modified, std::string& title,
                       std::string& author) {
  if (b.priorCount == 0) return false;
  const PriorEntry* const begin = b.prior.get();
  const PriorEntry* const end = begin + b.priorCount;
  const PriorEntry* hit =
      std::lower_bound(begin, end, hash, [](const PriorEntry& e, const uint64_t h) { return e.pathHash < h; });
  for (; hit != end && hit->pathHash == hash; ++hit) {
    if (hit->fileSize != size) continue;
    ClixRecord record;
    if (!b.previous.readRecord(hit->ordinal, record)) return false;
    if (record.modified != modified) continue;
    return b.previous.readTitle(record, title) && b.previous.readAuthor(record, author);
  }
  return false;
}

bool stageBook(Build& b, const char* path, const std::string_view name, const uint32_t size, const uint32_t modified) {
  BuildStats& stats = *b.stats;
  const size_t pathLen = std::strlen(path);
  if (pathLen > CLIX_MAX_PATH) {
    stats.skipped++;
    return true;
  }
  const uint64_t hash = clixPathHash(path, pathLen);
  for (size_t i = 0; i < b.seenCount; i++) {
    if (b.seen[i] == hash) {
      stats.duplicates++;
      return true;
    }
  }
  if (b.seenCount < b.seenCap) {
    b.seen[b.seenCount++] = hash;
  } else {
    stats.dedupDegraded = true;  // a book past the table may be listed twice on a damaged card
  }
  if (stats.books >= CLIX_MAX_RECORDS) {
    stats.truncated = true;
    b.stop = true;
    return true;
  }

  std::string title;
  std::string author;
  if (reuseFromPrevious(b, hash, size, modified, title, author)) {
    stats.reused++;
  } else {
    title.clear();
    author.clear();
    if (b.useMetadata && FsHelpers::hasEpubExtension(name)) {
      stats.parsed++;
      Epub epub(path, CACHE_DIR);
      if (!epub.loadMetadata(title, author)) {
        title.clear();
        author.clear();
      }
    }
    title = cleanText(title, CLIX_MAX_TITLE);
    author = cleanText(author, CLIX_MAX_AUTHOR);
    if (title.empty()) title = cleanText(stemOf(path), CLIX_MAX_TITLE);
    if (isUnknownAuthor(author)) author.clear();
  }

  StagedEntry& e = *b.entry;
  std::memset(&e, 0, sizeof(e));
  e.record.pathHash = hash;
  e.record.fileSize = size;
  e.record.modified = modified;
  copyField(e.title, sizeof(e.title), title, e.record.titleLen);
  copyField(e.author, sizeof(e.author), author, e.record.authorLen);
  std::memcpy(e.path, path, pathLen);
  e.record.pathLen = static_cast<uint8_t>(pathLen);
  if (e.record.titleLen == 0) {  // an extension with no stem (".epub" is hidden anyway): list the path
    copyField(e.title, sizeof(e.title), std::string(name), e.record.titleLen);
  }
  if (b.stage.write(&e, STAGE_STRIDE) != STAGE_STRIDE) {
    LOG_ERR("LIBIDX", "stage write failed");
    b.failed = true;
    return false;
  }
  b.nameBytes += static_cast<uint32_t>(e.record.titleLen) + e.record.authorLen + e.record.pathLen;
  stats.books++;
  return true;
}

// Iterative walk with a resume offset per level and no recursion, so stack use does not
// grow with depth and a directory that contains itself is cut off at LIBRARY_MAX_DEPTH.
bool walk(Build& b, const char* rootPath) {
  auto scan = makeUniqueNoThrow<Scan>();
  if (!scan) {
    LOG_ERR("LIBIDX", "OOM: walk state");
    return false;
  }
  if (std::strlen(rootPath) >= sizeof(scan->path)) return false;
  std::strcpy(scan->path, rootPath);
  HalFile dir = Storage.open(scan->path);
  if (!dir || !dir.isDirectory()) {
    LOG_ERR("LIBIDX", "cannot open %s", scan->path);
    return false;
  }
  const bool atRoot = std::strcmp(scan->path, "/") == 0;

  while (!b.stop && !b.failed) {
    tick(b);
    HalFile entry = dir.openNextFile();
    if (!entry) {
      if (scan->depth == 0) break;
      *std::strrchr(scan->path, '/') = '\0';
      dir = Storage.open(scan->path[0] ? scan->path : "/");
      if (!dir || !dir.isDirectory() || !dir.seekSet(scan->resume[--scan->depth])) {
        LOG_ERR("LIBIDX", "cannot resume %s", scan->path);
        return false;
      }
      continue;
    }
    const size_t length = entry.getName(scan->name, sizeof(scan->name));
    if (length == 0 || length >= sizeof(scan->name) - 1) {
      b.stats->skipped++;
      continue;
    }
    // Hidden entries (".crosspoint", AppleDouble "._x.epub"), and what the OS or another
    // reader writes, are never books.
    if (scan->name[0] == '.' || FsHelpers::isSystemFolderName(scan->name)) continue;
    const bool isDir = entry.isDirectory();
    if (isDir && atRoot && scan->depth == 0 && FsHelpers::isLibrarySkippedRootFolder(scan->name)) {
      continue;  // Lector's own folders at the card root hold no books
    }
    if (!isDir && !isLibraryBookName(scan->name)) continue;

    const size_t parentLength = std::strlen(scan->path);
    const size_t prefix = parentLength == 1 && scan->path[0] == '/' ? 0 : parentLength;
    if (prefix + 1 + length >= sizeof(scan->path)) {
      b.stats->skipped++;
      continue;
    }
    scan->path[prefix] = '/';
    std::memcpy(scan->path + prefix + 1, scan->name, length + 1);
    if (isDir) {
      if (scan->depth >= LIBRARY_MAX_DEPTH) {
        scan->path[prefix] = '\0';
        continue;
      }
      scan->resume[scan->depth++] = dir.position();
      dir = std::move(entry);
      continue;
    }
    const size_t fileSize = entry.fileSize();
    const uint32_t modified = packModified(entry);
    entry.close();  // before an Epub opens the same file
    stageBook(b, scan->path, std::string_view(scan->name, length),
              static_cast<uint32_t>(std::min<size_t>(fileSize, UINT32_MAX)), modified);
    scan->path[prefix] = '\0';
  }
  return !b.failed;
}

bool readStaged(Build& b, const uint16_t ordinal, StagedEntry& out) {
  return b.stage.seekSet(static_cast<size_t>(ordinal) * STAGE_STRIDE) &&
         b.stage.read(&out, STAGE_STRIDE) == static_cast<int>(STAGE_STRIDE);
}

// Sort keys by the first SORT_PREFIX bytes of the folded title, then settle each run of equal
// prefixes by the full folded title, read back from the staging file.
bool sortStaged(Build& b, SortKey* keys, const uint16_t count) {
  StagedEntry& e = *b.entry;
  for (uint16_t i = 0; i < count; i++) {
    if (!readStaged(b, i, e)) return false;
    std::memset(keys[i].prefix, 0, SORT_PREFIX);
    const std::string fold = sortFold(std::string_view(e.title, e.record.titleLen));
    std::memcpy(keys[i].prefix, fold.data(), std::min(fold.size(), SORT_PREFIX));
    keys[i].ordinal = i;
  }
  std::sort(keys, keys + count, sortKeyLess);

  struct RunItem {
    char fold[CLIX_MAX_TITLE + 1];
    uint16_t ordinal;
  };
  std::unique_ptr<RunItem[]> run;
  for (uint16_t i = 0; i < count;) {
    uint16_t j = i + 1;
    while (j < count && std::memcmp(keys[i].prefix, keys[j].prefix, SORT_PREFIX) == 0) j++;
    const size_t len = j - i;
    // Titles shorter than the prefix are equal when the prefixes are: nothing to settle.
    if (len > 1 && keys[i].prefix[SORT_PREFIX - 1] != '\0') {
      if (len > LIBRARY_MAX_TIE_RUN) {
        LOG_INF("LIBIDX", "%u titles share a prefix; kept in card order", static_cast<unsigned>(len));
      } else {
        if (!run) run = makeUniqueNoThrow<RunItem[]>(LIBRARY_MAX_TIE_RUN);
        if (!run) return false;
        for (size_t k = 0; k < len; k++) {
          if (!readStaged(b, keys[i + k].ordinal, e)) return false;
          const std::string fold = sortFold(std::string_view(e.title, e.record.titleLen));
          std::memcpy(run[k].fold, fold.data(), fold.size());
          run[k].fold[fold.size()] = '\0';
          run[k].ordinal = keys[i + k].ordinal;
        }
        std::sort(run.get(), run.get() + len, [](const RunItem& x, const RunItem& y) {
          const int c = std::strcmp(x.fold, y.fold);
          return c != 0 ? c < 0 : x.ordinal < y.ordinal;
        });
        for (size_t k = 0; k < len; k++) keys[i + k].ordinal = run[k].ordinal;
      }
    }
    i = j;
  }
  return true;
}

bool writeAll(HalFile& out, const void* data, const size_t len) { return len == 0 || out.write(data, len) == len; }

bool writeZeros(HalFile& out, size_t len) {
  static constexpr uint8_t ZEROS[64] = {};
  while (len > 0) {
    const size_t n = std::min(len, sizeof(ZEROS));
    if (!writeAll(out, ZEROS, n)) return false;
    len -= n;
  }
  return true;
}

bool emitIndex(Build& b, const SortKey* keys, const uint16_t count, const std::string& partPath) {
  HalFile out;
  if (!Storage.openFileForWrite("LIBIDX", partPath, out)) {
    LOG_ERR("LIBIDX", "cannot create %s", partPath.c_str());
    return false;
  }
  ClixHeader head{};
  std::memcpy(head.magic, CLIX_MAGIC, sizeof(head.magic));
  head.formatVersion = CLIX_FORMAT_VERSION;
  head.flags = (b.stats->truncated ? CLIX_FLAG_TRUNCATED : 0) | (b.useMetadata ? 0 : CLIX_FLAG_NO_METADATA);
  head.bookCount = count;
  layoutSections(head, b.nameBytes);

  bool ok = writeAll(out, &head, sizeof(head)) && writeZeros(out, head.recordStart - sizeof(head));

  // Records, in sorted order. Each carries the running offset of its text.
  StagedEntry& e = *b.entry;
  uint32_t nameOff = 0;
  for (uint16_t i = 0; ok && i < count; i++) {
    ok = readStaged(b, keys[i].ordinal, e);
    if (!ok) break;
    e.record.nameOff = nameOff;
    nameOff += static_cast<uint32_t>(e.record.titleLen) + e.record.authorLen + e.record.pathLen;
    ok = writeAll(out, &e.record, sizeof(ClixRecord));
  }
  ok = ok && writeZeros(out, head.nameStart - (head.recordStart + static_cast<uint32_t>(count) * sizeof(ClixRecord)));

  // Text, in the same order.
  for (uint16_t i = 0; ok && i < count; i++) {
    ok = readStaged(b, keys[i].ordinal, e) && writeAll(out, e.title, e.record.titleLen) &&
         writeAll(out, e.author, e.record.authorLen) && writeAll(out, e.path, e.record.pathLen);
  }
  out.flush();
  const bool whole = ok && out.position() == head.selfSize;
  out.close();
  if (!whole) LOG_ERR("LIBIDX", "index write failed or short");
  return whole;
}

}  // namespace

const char* libraryIndexPath() { return INDEX_PATH; }

bool isLibraryBookName(const std::string_view name) {
  return !name.empty() && name[0] != '.' && FsHelpers::hasBookExtension(name) && !isQuotesSidecar(name);
}

bool buildLibraryIndex(const char* rootPath, const char* indexPath, BuildStats& stats, ProgressFn progress,
                       void* progressCtx, const bool useMetadata) {
  stats = {};
  auto b = makeUniqueNoThrow<Build>();
  if (!b) {
    LOG_ERR("LIBIDX", "OOM: build state");
    return false;
  }
  b->indexPath = indexPath;
  b->stats = &stats;
  b->progress = progress;
  b->progressCtx = progressCtx;
  b->useMetadata = useMetadata;
  b->entry = makeUniqueNoThrow<StagedEntry>();
  if (!b->entry) {
    LOG_ERR("LIBIDX", "OOM: staged entry");
    return false;
  }
  Storage.ensureDirectoryExists(CACHE_DIR);

  if (!loadPrior(*b)) return false;
  // The paths seen: room for every book the previous index held, and some new ones. A first
  // build (no previous index) covers 1024 books; the next one covers what it found.
  b->seenCap = std::min<size_t>(CLIX_MAX_RECORDS, std::max<size_t>(b->priorCount + b->priorCount / 4, 1024));
  b->seen = makeUniqueNoThrow<uint64_t[]>(b->seenCap);
  if (!b->seen) b->seenCap = 0;

  if (!Storage.openFileForWrite("LIBIDX", STAGE_PATH, b->stage)) {
    LOG_ERR("LIBIDX", "cannot create %s", STAGE_PATH);
    return false;
  }
  const bool walked = walk(*b, rootPath);
  b->stage.flush();
  b->stage.close();
  b->previous.close();
  b->prior.reset();
  b->seen.reset();

  bool built = false;
  const uint16_t count = stats.books;
  if (walked && Storage.openFileForRead("LIBIDX", STAGE_PATH, b->stage)) {
    auto keys = makeUniqueNoThrow<SortKey[]>(count == 0 ? 1 : count);
    if (!keys) {
      LOG_ERR("LIBIDX", "OOM: %u sort keys", static_cast<unsigned>(count));
    } else if (sortStaged(*b, keys.get(), count)) {
      const std::string partPath = FsHelpers::partialPathFor(indexPath);
      built = emitIndex(*b, keys.get(), count, partPath);
      if (built) {
        // FAT rename never overwrites: the old index goes first. A reset between the two
        // leaves no index, which the next entry rebuilds, never a half-written one.
        built =
            (!Storage.exists(indexPath) || Storage.remove(indexPath)) && Storage.rename(partPath.c_str(), indexPath);
      }
      if (!built) Storage.remove(partPath.c_str());
    }
  }
  b->stage.close();
  Storage.remove(STAGE_PATH);
  LOG_INF("LIBIDX", "%s: %u books (%u reused, %u parsed, %u duplicate, %u skipped%s)", built ? "built" : "FAILED",
          static_cast<unsigned>(stats.books), static_cast<unsigned>(stats.reused), static_cast<unsigned>(stats.parsed),
          static_cast<unsigned>(stats.duplicates), static_cast<unsigned>(stats.skipped),
          stats.truncated ? ", truncated" : "");
  return built;
}

}  // namespace library
