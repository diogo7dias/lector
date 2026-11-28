#include "LibraryIndexFile.h"

#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

namespace library {

bool LibraryIndexFile::open(const char* path) {
  close();
  lastValidity = ClixValidity::BadMagic;
  readFailed = false;
  if (!Storage.openFileForRead("LIBIDX", path, file)) return false;

  const uint64_t size = file.fileSize64();
  if (size < sizeof(ClixHeader) || !readAt(0, &head, sizeof(head))) {
    LOG_ERR("LIBIDX", "cannot read header of %s", path);
    close();
    return false;
  }
  lastValidity = validateHeader(head, size);
  if (lastValidity != ClixValidity::Ok) {
    LOG_INF("LIBIDX", "index rejected: %s", clixValidityName(lastValidity));
    close();
    return false;
  }
  opened = true;
  return true;
}

void LibraryIndexFile::close() {
  file.close();
  opened = false;
}

bool LibraryIndexFile::readAt(const uint32_t offset, void* dst, const size_t len) {
  if (!file.seekSet(offset) || file.read(dst, len) != static_cast<int>(len)) {
    readFailed = true;
    return false;
  }
  return true;
}

size_t LibraryIndexFile::readRecords(const uint16_t first, size_t count, ClixRecord* out) {
  if (!opened || first >= head.bookCount) return 0;
  count = std::min<size_t>(count, head.bookCount - first);
  if (!readAt(recordOffset(head, first), out, count * sizeof(ClixRecord))) return 0;
  for (size_t i = 0; i < count; i++) {
    if (!recordInBounds(head, out[i])) {
      LOG_ERR("LIBIDX", "record %u out of bounds", static_cast<unsigned>(first + i));
      return i;
    }
  }
  return count;
}

bool LibraryIndexFile::readText(const ClixRecord& record, const size_t skip, const size_t len, std::string& out) {
  out.clear();
  if (!opened || len == 0) return opened;
  out.resize(len);
  if (!readAt(head.nameStart + record.nameOff + static_cast<uint32_t>(skip), out.data(), len)) {
    out.clear();
    return false;
  }
  return true;
}

bool LibraryIndexFile::readTitle(const ClixRecord& record, std::string& out) {
  if (!readText(record, 0, record.titleLen, out)) return false;
  out = cleanText(out, CLIX_MAX_TITLE);
  return true;
}

bool LibraryIndexFile::readAuthor(const ClixRecord& record, std::string& out) {
  if (!readText(record, record.titleLen, record.authorLen, out)) return false;
  out = cleanText(out, CLIX_MAX_AUTHOR);
  return true;
}

bool LibraryIndexFile::readPath(const ClixRecord& record, std::string& out) {
  return readText(record, static_cast<size_t>(record.titleLen) + record.authorLen, record.pathLen, out) &&
         !out.empty() && out[0] == '/';
}

bool LibraryIndexFile::readSeries(const ClixRecord& record, std::string& out) {
  if (!readText(record, static_cast<size_t>(record.titleLen) + record.authorLen + record.pathLen, record.seriesLen,
                out)) {
    return false;
  }
  out = cleanText(out, CLIX_MAX_SERIES);
  return true;
}

bool updateRecordProgress(const char* indexPath, const char* bookPath, const uint8_t percent,
                          const uint32_t readOrder) {
  if (!indexPath || !bookPath || bookPath[0] != '/') return false;
  const size_t pathLen = std::strlen(bookPath);
  if (pathLen == 0 || pathLen > CLIX_MAX_PATH) return false;

  HalFile file = Storage.open(indexPath, O_RDWR);
  if (!file) return false;

  const uint64_t size = file.fileSize64();
  ClixHeader head{};
  if (size < sizeof(ClixHeader) || !file.seekSet(0) ||
      file.read(&head, sizeof(head)) != static_cast<int>(sizeof(head))) {
    return false;
  }
  if (validateHeader(head, size) != ClixValidity::Ok || head.bookCount == 0) return false;

  const uint64_t targetHash = clixPathHash(bookPath, pathLen);
  constexpr size_t BATCH = 4;  // 128 B on stack (under the C3's 256 B local cap)
  ClixRecord batch[BATCH];
  std::string storedPath;

  for (uint16_t first = 0; first < head.bookCount; first += BATCH) {
    const size_t wanted = std::min<size_t>(BATCH, head.bookCount - first);
    const size_t bytes = wanted * sizeof(ClixRecord);
    if (!file.seekSet(recordOffset(head, first)) || file.read(batch, bytes) != static_cast<int>(bytes)) {
      return false;
    }
    for (size_t i = 0; i < wanted; ++i) {
      ClixRecord& r = batch[i];
      if (r.pathHash != targetHash || r.pathLen != pathLen) continue;
      if (!recordInBounds(head, r)) continue;
      storedPath.resize(r.pathLen);
      const uint32_t pathOff = head.nameStart + r.nameOff + static_cast<uint32_t>(r.titleLen) + r.authorLen;
      if (!file.seekSet(pathOff) || file.read(storedPath.data(), r.pathLen) != static_cast<int>(r.pathLen)) {
        return false;
      }
      if (storedPath != bookPath) continue;

      if (r.percent == percent && r.readOrder == readOrder) return true;
      r.percent = percent;
      r.readOrder = readOrder;
      const uint32_t recOff = recordOffset(head, static_cast<uint16_t>(first + i));
      if (!file.seekSet(recOff) || file.write(&r, sizeof(ClixRecord)) != sizeof(ClixRecord)) {
        LOG_ERR("LIBIDX", "in-place progress write failed");
        return false;
      }
      file.flush();
      return true;
    }
  }
  return false;
}

bool buildLibraryView(LibraryIndexFile& index, const LibrarySort sort, const LibraryFilter filter,
                      std::vector<uint16_t>& out, ViewProgressFn progress, void* progressCtx) {
  out.clear();
  if (!index.isOpen()) return false;
  const uint16_t total = index.bookCount();
  if (total == 0 || (sort == LibrarySort::Title && filter == LibraryFilter::All)) return true;

  constexpr size_t BATCH = 16;
  auto batch = makeUniqueNoThrow<ClixRecord[]>(BATCH);
  if (!batch) {
    LOG_ERR("LIBIDX", "OOM: view batch");
    return false;
  }

  // Pass 1: count matching records so the output vector does one exact allocation.
  size_t matchCount = 0;
  if (filter == LibraryFilter::All) {
    matchCount = total;
  } else {
    for (uint16_t first = 0; first < total; first += BATCH) {
      if (progress) progress(progressCtx);
      const size_t wanted = std::min<size_t>(BATCH, total - first);
      const size_t got = index.readRecords(first, wanted, batch.get());
      for (size_t i = 0; i < got; ++i) {
        if (matchesFilter(batch[i], filter)) matchCount++;
      }
      if (got < wanted) break;
    }
  }
  if (matchCount == 0) return true;

  // Probe allocation with nothrow before reserving `out` (throwing std::vector aborts on ESP32).
  {
    auto probe = makeUniqueNoThrow<uint16_t[]>(matchCount);
    if (!probe) {
      LOG_ERR("LIBIDX", "OOM: %u view ordinals", static_cast<unsigned>(matchCount));
      return false;
    }
  }
  out.reserve(matchCount);

  if (sort == LibrarySort::Title) {
    for (uint16_t first = 0; first < total; first += BATCH) {
      if (progress) progress(progressCtx);
      const size_t wanted = std::min<size_t>(BATCH, total - first);
      const size_t got = index.readRecords(first, wanted, batch.get());
      for (size_t i = 0; i < got; ++i) {
        if (matchesFilter(batch[i], filter)) out.push_back(static_cast<uint16_t>(first + i));
      }
      if (got < wanted) break;
    }
    return true;
  }

  if (sort == LibrarySort::RecentlyAdded || sort == LibrarySort::LastRead) {
    struct NumKey {
      uint32_t value;
      uint16_t ordinal;
    };
    auto keys = makeUniqueNoThrow<NumKey[]>(matchCount);
    if (!keys) {
      LOG_ERR("LIBIDX", "OOM: %u sort keys", static_cast<unsigned>(matchCount));
      return false;
    }
    size_t count = 0;
    for (uint16_t first = 0; first < total && count < matchCount; first += BATCH) {
      if (progress) progress(progressCtx);
      const size_t wanted = std::min<size_t>(BATCH, total - first);
      const size_t got = index.readRecords(first, wanted, batch.get());
      for (size_t i = 0; i < got && count < matchCount; ++i) {
        if (!matchesFilter(batch[i], filter)) continue;
        const uint32_t key = sort == LibrarySort::RecentlyAdded ? batch[i].modified : batch[i].readOrder;
        keys[count++] = NumKey{key, static_cast<uint16_t>(first + i)};
      }
      if (got < wanted) break;
    }
    std::sort(keys.get(), keys.get() + count, [](const NumKey& a, const NumKey& b) {
      return a.value != b.value ? a.value > b.value : a.ordinal < b.ordinal;
    });
    for (size_t i = 0; i < count; ++i) out.push_back(keys[i].ordinal);
    return true;
  }

  // LibrarySort::Author: 16-byte folded author prefix, empty authors at the bottom,
  // tie runs settled by full folded author and then by ordinal (title order).
  constexpr size_t AUTHOR_PREFIX = 16;
  constexpr size_t MAX_TIE_RUN = 64;
  struct AuthorKey {
    char prefix[AUTHOR_PREFIX];
    uint16_t ordinal;
  };
  auto keys = makeUniqueNoThrow<AuthorKey[]>(matchCount);
  if (!keys) {
    LOG_ERR("LIBIDX", "OOM: %u author keys", static_cast<unsigned>(matchCount));
    return false;
  }
  size_t count = 0;
  std::string author;
  for (uint16_t first = 0; first < total && count < matchCount; first += BATCH) {
    if (progress) progress(progressCtx);
    const size_t wanted = std::min<size_t>(BATCH, total - first);
    const size_t got = index.readRecords(first, wanted, batch.get());
    for (size_t i = 0; i < got && count < matchCount; ++i) {
      if (!matchesFilter(batch[i], filter)) continue;
      AuthorKey& k = keys[count++];
      std::memset(k.prefix, 0, AUTHOR_PREFIX);
      k.ordinal = static_cast<uint16_t>(first + i);
      if (batch[i].authorLen == 0) {
        k.prefix[0] = '\x7F';  // sort authorless books after named authors
      } else if (index.readAuthor(batch[i], author) && !author.empty()) {
        const std::string fold = sortFold(author);
        std::memcpy(k.prefix, fold.data(), std::min(fold.size(), AUTHOR_PREFIX));
      } else {
        k.prefix[0] = '\x7F';
      }
    }
    if (got < wanted) break;
  }
  std::sort(keys.get(), keys.get() + count, [](const AuthorKey& a, const AuthorKey& b) {
    const int c = std::memcmp(a.prefix, b.prefix, AUTHOR_PREFIX);
    return c != 0 ? c < 0 : a.ordinal < b.ordinal;
  });

  struct TieItem {
    char fold[CLIX_MAX_AUTHOR + 1];
    uint16_t ordinal;
  };
  std::unique_ptr<TieItem[]> run;
  for (size_t i = 0; i < count;) {
    size_t j = i + 1;
    while (j < count && std::memcmp(keys[i].prefix, keys[j].prefix, AUTHOR_PREFIX) == 0) j++;
    const size_t len = j - i;
    if (len > 1 && keys[i].prefix[AUTHOR_PREFIX - 1] != '\0' && len <= MAX_TIE_RUN) {
      if (!run) run = makeUniqueNoThrow<TieItem[]>(MAX_TIE_RUN);
      if (run) {
        bool ok = true;
        for (size_t k = 0; k < len; ++k) {
          ClixRecord rec;
          if (!index.readRecord(keys[i + k].ordinal, rec) || !index.readAuthor(rec, author)) {
            ok = false;
            break;
          }
          const std::string fold = sortFold(author);
          const size_t n = std::min(fold.size(), CLIX_MAX_AUTHOR);
          std::memcpy(run[k].fold, fold.data(), n);
          run[k].fold[n] = '\0';
          run[k].ordinal = keys[i + k].ordinal;
        }
        if (ok) {
          std::sort(run.get(), run.get() + len, [](const TieItem& x, const TieItem& y) {
            const int c = std::strcmp(x.fold, y.fold);
            return c != 0 ? c < 0 : x.ordinal < y.ordinal;
          });
          for (size_t k = 0; k < len; ++k) keys[i + k].ordinal = run[k].ordinal;
        }
      }
    }
    i = j;
  }

  for (size_t i = 0; i < count; ++i) out.push_back(keys[i].ordinal);
  return true;
}

}  // namespace library
