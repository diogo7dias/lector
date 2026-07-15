#pragma once

#include <Epub.h>
#include <Logging.h>

#include "ProgressFile.h"

namespace EpubReaderUtils {

// Persists reader progress for an EPUB to its cache directory. Returns true on success.
// `bookPercent` (0-100) is stored as a trailing 7th byte so the Home [NN%] badge can be
// recovered from the sidecar when recent.json loses it (firmware/store migration);
// pass -1 for unknown (stored as 255). Old 6-byte sidecars stay readable — the byte
// is optional on read — and old firmware simply ignores the extra byte.
inline bool saveProgress(const Epub& epub, int spineIndex, int pageNumber, int pageCount, int bookPercent = -1) {
  if (spineIndex < 0 || spineIndex > 0xFFFF || pageNumber < 0 || pageNumber > 0xFFFF || pageCount < 0 ||
      pageCount > 0xFFFF) {
    LOG_ERR("ERS", "Progress values out of range: spine=%d page=%d count=%d", spineIndex, pageNumber, pageCount);
    return false;
  }
  uint8_t data[7];
  data[0] = spineIndex & 0xFF;
  data[1] = (spineIndex >> 8) & 0xFF;
  data[2] = pageNumber & 0xFF;
  data[3] = (pageNumber >> 8) & 0xFF;
  data[4] = pageCount & 0xFF;
  data[5] = (pageCount >> 8) & 0xFF;
  data[6] = (bookPercent >= 0 && bookPercent <= 100) ? static_cast<uint8_t>(bookPercent) : 255;
  if (!ProgressFile::writeAtomic(epub.getCachePath(), data, sizeof(data))) {
    return false;
  }
  LOG_DBG("ERS", "Progress saved: spine=%d page=%d", spineIndex, pageNumber);
  return true;
}

}  // namespace EpubReaderUtils
