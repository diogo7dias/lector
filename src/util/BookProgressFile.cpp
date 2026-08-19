#include "BookProgressFile.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstring>

#include "RecentBooksStore.h"
#include "util/BookCacheUtils.h"

namespace book_progress {
namespace {
constexpr const char* FILE_NAME = "/percent.bin";
// percent, then the read counter little-endian. Read back with memcpy: the ESP32-C3 faults
// on an unaligned multi-byte load, and byte 1 of a buffer is never 4-byte aligned.
constexpr size_t RECORD_BYTES = 5;

void encode(const Marker& marker, uint8_t (&bytes)[RECORD_BYTES]) {
  bytes[0] = marker.percent;
  bytes[1] = static_cast<uint8_t>(marker.readOrder);
  bytes[2] = static_cast<uint8_t>(marker.readOrder >> 8);
  bytes[3] = static_cast<uint8_t>(marker.readOrder >> 16);
  bytes[4] = static_cast<uint8_t>(marker.readOrder >> 24);
}
}  // namespace

bool write(const std::string& cacheDir, const Marker& marker) {
  if (cacheDir.empty()) return true;  // a format with no cache has nothing to mark

  uint8_t bytes[RECORD_BYTES];
  encode(marker, bytes);

  const std::string path = cacheDir + FILE_NAME;
  // Read first and skip an unchanged record: this runs on every reader exit, and the SD
  // card's erase cycles are worth more than five bytes of freshness.
  {
    HalFile current = Storage.open(path.c_str());
    uint8_t existing[RECORD_BYTES] = {};
    if (current && current.read(existing, RECORD_BYTES) == static_cast<int>(RECORD_BYTES) &&
        std::memcmp(existing, bytes, RECORD_BYTES) == 0) {
      return true;
    }
  }

  HalFile file;
  if (!Storage.openFileForWrite("BPRG", path, file)) {
    LOG_ERR("BPRG", "Could not write progress marker: %s", path.c_str());
    return false;
  }
  return file.write(bytes, RECORD_BYTES) == RECORD_BYTES;
}

bool readForBook(const std::string& bookPath, Marker& markerOut) {
  const std::string cacheDir = bookCacheDirForPath(bookPath);
  if (cacheDir.empty()) return false;

  HalFile file = Storage.open((cacheDir + FILE_NAME).c_str());
  if (!file) return false;

  uint8_t bytes[RECORD_BYTES] = {};
  const int read = file.read(bytes, RECORD_BYTES);
  if (read < 1) return false;
  if (bytes[0] > 100) return false;

  markerOut.percent = bytes[0];
  // A record written before the counter existed is one byte long; its order is unknown,
  // which sorts it below every book read since.
  markerOut.readOrder = 0;
  if (read == static_cast<int>(RECORD_BYTES)) {
    markerOut.readOrder = static_cast<uint32_t>(bytes[1]) | (static_cast<uint32_t>(bytes[2]) << 8) |
                          (static_cast<uint32_t>(bytes[3]) << 16) | (static_cast<uint32_t>(bytes[4]) << 24);
  }
  return true;
}

void backfillFromRecents() {
  int seeded = 0;
  for (const auto& book : RECENT_BOOKS.getBooks()) {
    if (book.progressPercent < 0) continue;  // never read far enough to have a number
    if (RecentBooksStore::isMissing(book)) continue;

    const std::string cacheDir = bookCacheDirForPath(book.path);
    if (cacheDir.empty()) continue;
    // Only seed what has nothing: a book read since the upgrade already carries a marker
    // with a real read order, and this would flatten it.
    Marker existing;
    if (readForBook(book.path, existing)) continue;

    Marker marker;
    marker.percent = static_cast<uint8_t>(book.progressPercent > 100 ? 100 : book.progressPercent);
    if (write(cacheDir, marker)) seeded++;
  }
  LOG_INF("BPRG", "Seeded %d reading badges from the recents list", seeded);
}

}  // namespace book_progress
