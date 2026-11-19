#include "LibraryIndexFile.h"

#include <Logging.h>

#include <algorithm>

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

}  // namespace library
