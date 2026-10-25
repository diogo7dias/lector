#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Serialization.h>
#include <Utf8.h>

#include <algorithm>

#include "TxtReaderActivity.h"
#include "components/UITheme.h"

namespace {
constexpr size_t CHUNK_SIZE = 8 * 1024;  // 8KB chunk for reading
// Cache file magic and version
constexpr uint32_t CACHE_MAGIC = 0x54585449;  // "TXTI"
// Increment when cache format changes (4: form-feed page breaks, 5: UTF-16 refused,
// 6: Windows-1252 files decoded, so their pages wrap at different points)
constexpr uint8_t CACHE_VERSION = 6;
// How far into a file detectEncoding() looks for a non-ASCII byte before calling it UTF-8.
constexpr size_t ENCODING_SCAN_BYTES = 64 * 1024;
}  // namespace

void TxtReaderActivity::detectEncoding() {
  // A file that is not valid UTF-8 is read as Windows-1252, the usual encoding of older
  // Western text files. Plain ASCII up to the scan limit reads the same either way.
  legacy8Bit = false;
  const size_t fileSize = txt->getFileSize();
  auto buf = makeUniqueNoThrow<uint8_t[]>(CHUNK_SIZE);
  if (!buf) {
    LOG_ERR("TRS", "OOM: encoding scan buffer");
    return;
  }
  for (size_t offset = 0; offset < std::min(fileSize, ENCODING_SCAN_BYTES); offset += CHUNK_SIZE) {
    const size_t len = std::min(CHUNK_SIZE, fileSize - offset);
    if (!txt->readContent(buf.get(), offset, len)) return;
    const auto guess = utf8GuessEncoding(buf.get(), len);
    if (guess == TextEncodingGuess::Undecided) continue;
    legacy8Bit = guess == TextEncodingGuess::Legacy8Bit;
    break;
  }
  if (legacy8Bit) LOG_INF("TRS", "Not UTF-8: reading as Windows-1252");
}

void TxtReaderActivity::buildPageIndex() {
  pageOffsets.clear();
  paraPagesFor = -1;

  size_t offset = 0;
  const size_t fileSize = txt->getFileSize();

  // UTF-16 puts a NUL after every ASCII byte, so each line would draw one character.
  // Refuse it with a message rather than show that.
  uint8_t bom[2] = {0, 0};
  if (fileSize >= 2 && txt->readContent(bom, 0, 2) &&
      ((bom[0] == 0xFF && bom[1] == 0xFE) || (bom[0] == 0xFE && bom[1] == 0xFF))) {
    LOG_ERR("TRS", "UTF-16 text is not supported");
    unsupportedEncoding = true;
    totalPages = 0;
    return;
  }

  pageOffsets.push_back(0);  // First page starts at offset 0

  LOG_DBG("TRS", "Building page index for %zu bytes...", fileSize);

  if (!quietIndex_) GUI.drawPopup(renderer, tr(STR_INDEXING));
  quietIndex_ = false;

  while (offset < fileSize) {
    std::vector<std::string> tempLines;
    size_t nextOffset = offset;

    if (!loadPageAtOffset(offset, tempLines, nextOffset)) {
      break;
    }

    if (nextOffset <= offset) {
      // No progress made, avoid infinite loop
      break;
    }

    offset = nextOffset;
    if (offset < fileSize) {
      pageOffsets.push_back(offset);
    }

    // Yield to other tasks periodically
    if (pageOffsets.size() % 20 == 0) {
      vTaskDelay(1);
    }
  }

  // Nothing past offset 0 (a 0-byte file, or one that is only a byte-order mark): no
  // pages, so render shows the empty-file message instead of one blank page.
  if (offset == 0) pageOffsets.clear();

  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Built page index: %d pages", totalPages);
}

bool TxtReaderActivity::loadPageAtOffset(size_t offset, std::vector<std::string>& outLines, size_t& nextOffset) {
  outLines.clear();
  const size_t fileSize = txt->getFileSize();

  if (offset >= fileSize) {
    return false;
  }

  // Read a chunk from file
  size_t chunkSize = std::min(CHUNK_SIZE, fileSize - offset);
  auto* buffer = static_cast<uint8_t*>(malloc(chunkSize + 1));
  if (!buffer) {
    LOG_ERR("TRS", "Failed to allocate %zu bytes", chunkSize);
    return false;
  }

  if (!txt->readContent(buffer, offset, chunkSize)) {
    free(buffer);
    return false;
  }
  buffer[chunkSize] = '\0';

  // A Windows-1252 chunk is decoded to UTF-8 first; everything below then works on the
  // decoded text, and the final position is mapped back to file bytes (one byte per
  // character in that encoding).
  std::string decoded;
  if (legacy8Bit) {
    decoded = cp1252ToUtf8(reinterpret_cast<const char*>(buffer), chunkSize);
    free(buffer);
    buffer = reinterpret_cast<uint8_t*>(decoded.data());
  }
  const bool chunkReachesEof = offset + chunkSize >= fileSize;
  const size_t textLen = legacy8Bit ? decoded.size() : chunkSize;

  // Prime the SD card font's advance table with this chunk's codepoints.
  // Without this, every getTextAdvanceX() call in the wrap loop below triggers
  // on-demand glyph loads through the 8-slot overflow ring buffer, which
  // thrashes for any text with more than 8 unique chars (i.e. all English),
  // floods the heap with short-lived bitmap allocations, and eventually
  // corrupts FreeRTOS state. The advance table persists across calls per
  // font, so the cost amortizes to ~ASCII-size after the first chunk.
  if (renderer.isSdCardFont(cachedFontId)) {
    renderer.ensureSdCardFontReady(cachedFontId, reinterpret_cast<const char*>(buffer), /*styleMask=*/0x01);
  }

  // Parse lines from buffer. A UTF-8 byte-order mark is not text: skip it, or the
  // first line opens with a stray glyph.
  size_t pos =
      (!legacy8Bit && offset == 0 && chunkSize >= 3 && buffer[0] == 0xEF && buffer[1] == 0xBB && buffer[2] == 0xBF) ? 3
                                                                                                                    : 0;

  while (pos < textLen && static_cast<int>(outLines.size()) < linesPerPage) {
    // Find end of line
    size_t lineEnd = pos;
    while (lineEnd < textLen && buffer[lineEnd] != '\n') {
      lineEnd++;
    }

    // Check if we have a complete line
    bool lineComplete = (lineEnd < textLen) || chunkReachesEof;

    if (!lineComplete && static_cast<int>(outLines.size()) > 0) {
      // Incomplete line and we already have some lines, stop here
      break;
    }

    // Form feed = hard page break. The quotes sidecar writes one before every quote,
    // so each saved quote opens its own page instead of running on from the last one.
    // Reached with lines already on the page: stop, leaving the marker as the next
    // page's first byte. Reached on an empty page: swallow it and fill this page.
    if (buffer[pos] == '\f') {
      if (!outLines.empty()) break;
      pos++;
      continue;
    }

    // Calculate the actual length of line content in the buffer (excluding newline)
    size_t lineContentLen = lineEnd - pos;

    // Check for carriage return
    bool hasCR = (lineContentLen > 0 && buffer[pos + lineContentLen - 1] == '\r');
    size_t displayLen = hasCR ? lineContentLen - 1 : lineContentLen;

    // Extract line content for display (without CR/LF)
    std::string line(reinterpret_cast<char*>(buffer + pos), displayLen);

    // Track position within this source line (in bytes from pos)
    size_t lineBytePos = 0;

    // Emit at least one visual line for each source line (including blank lines),
    // then continue with wrapping when needed.
    do {
      if (line.empty()) {
        outLines.emplace_back();
        break;
      }

      int lineWidth = renderer.getTextAdvanceX(cachedFontId, line.c_str(), EpdFontFamily::REGULAR);

      if (lineWidth <= viewportWidth) {
        outLines.push_back(line);
        lineBytePos = displayLen;  // Consumed entire display content
        line.clear();
        break;
      }

      // Find break point
      size_t breakPos = line.length();
      while (breakPos > 0 && renderer.getTextAdvanceX(cachedFontId, line.substr(0, breakPos).c_str(),
                                                      EpdFontFamily::REGULAR) > viewportWidth) {
        // Try to break at space
        size_t spacePos = line.rfind(' ', breakPos - 1);
        if (spacePos != std::string::npos && spacePos > 0) {
          breakPos = spacePos;
        } else {
          // Break at character boundary for UTF-8
          breakPos--;
          // Make sure we don't break in the middle of a UTF-8 sequence
          while (breakPos > 0 && (line[breakPos] & 0xC0) == 0x80) {
            breakPos--;
          }
        }
      }

      if (breakPos == 0) {
        breakPos = 1;
      }

      outLines.push_back(line.substr(0, breakPos));

      // Skip space at break point
      size_t skipChars = breakPos;
      if (breakPos < line.length() && line[breakPos] == ' ') {
        skipChars++;
      }
      lineBytePos += skipChars;
      line = line.substr(skipChars);
    } while (!line.empty() && static_cast<int>(outLines.size()) < linesPerPage);

    // Determine how much of the source buffer we consumed
    if (line.empty()) {
      // Fully consumed this source line, move past the newline
      pos = lineEnd + 1;
    } else {
      // Partially consumed - page is full mid-line
      // Move pos to where we stopped in the line (NOT past the line)
      pos = pos + lineBytePos;
      break;
    }
  }

  // Ensure we make progress even if calculations go wrong
  if (pos == 0 && !outLines.empty()) {
    // Fallback: at minimum, consume something to avoid infinite loop
    pos = 1;
  }

  if (legacy8Bit) {
    // Decoded bytes back to file bytes: one per character, so count the lead bytes.
    size_t fileBytes = 0;
    for (size_t i = 0; i < pos && i < textLen; ++i) {
      if ((buffer[i] & 0xC0) != 0x80) ++fileBytes;
    }
    pos = fileBytes;
  }
  nextOffset = offset + pos;

  // Make sure we don't go past the file
  if (nextOffset > fileSize) {
    nextOffset = fileSize;
  }

  if (!legacy8Bit) free(buffer);  // a decoded chunk lives in `decoded`

  return !outLines.empty();
}

bool TxtReaderActivity::loadPageIndexCache() {
  // Cache file format (using serialization module):
  // - uint32_t: magic "TXTI"
  // - uint8_t: cache version
  // - uint32_t: file size (to validate cache)
  // - int32_t: viewport width
  // - int32_t: lines per page
  // - int32_t: font ID (to invalidate cache on font change)
  // - int32_t: screen margin (to invalidate cache on margin change)
  // - uint8_t: paragraph alignment (to invalidate cache on alignment change)
  // - uint32_t: total pages count
  // - N * uint32_t: page offsets

  std::string cachePath = txt->getCachePath() + "/index.bin";
  HalFile f;
  if (!Storage.openFileForRead("TRS", cachePath, f)) {
    LOG_DBG("TRS", "No page index cache found");
    return false;
  }

  // Read and validate header using serialization module
  uint32_t magic;
  serialization::readPod(f, magic);
  if (magic != CACHE_MAGIC) {
    LOG_DBG("TRS", "Cache magic mismatch, rebuilding");
    return false;
  }

  uint8_t version;
  serialization::readPod(f, version);
  if (version != CACHE_VERSION) {
    LOG_DBG("TRS", "Cache version mismatch (%d != %d), rebuilding", version, CACHE_VERSION);
    return false;
  }

  uint32_t fileSize;
  serialization::readPod(f, fileSize);
  if (fileSize != txt->getFileSize()) {
    LOG_DBG("TRS", "Cache file size mismatch, rebuilding");
    return false;
  }

  int32_t cachedWidth;
  serialization::readPod(f, cachedWidth);
  if (cachedWidth != viewportWidth) {
    LOG_DBG("TRS", "Cache viewport width mismatch, rebuilding");
    return false;
  }

  int32_t cachedLines;
  serialization::readPod(f, cachedLines);
  if (cachedLines != linesPerPage) {
    LOG_DBG("TRS", "Cache lines per page mismatch, rebuilding");
    return false;
  }

  int32_t fontId;
  serialization::readPod(f, fontId);
  if (fontId != cachedFontId) {
    LOG_DBG("TRS", "Cache font ID mismatch (%d != %d), rebuilding", fontId, cachedFontId);
    return false;
  }

  int32_t margin;
  serialization::readPod(f, margin);
  if (margin != cachedScreenMargin) {
    LOG_DBG("TRS", "Cache screen margin mismatch, rebuilding");
    return false;
  }

  uint8_t alignment;
  serialization::readPod(f, alignment);
  if (alignment != cachedParagraphAlignment) {
    LOG_DBG("TRS", "Cache paragraph alignment mismatch, rebuilding");
    return false;
  }

  // A truncated index (card pulled mid-save) must not size reserve() or leave
  // zeroed offsets: bound the count by the bytes actually left, check each read.
  uint32_t numPages;
  const size_t here = f.position();
  const size_t total = f.size();
  if (!serialization::readPod(f, numPages) || numPages == 0 || total < here + sizeof(numPages) ||
      numPages > (total - here - sizeof(numPages)) / sizeof(uint32_t)) {
    LOG_DBG("TRS", "Cache page count unusable, rebuilding");
    return false;
  }

  // Read page offsets
  pageOffsets.clear();
  paraPagesFor = -1;
  pageOffsets.reserve(numPages);

  for (uint32_t i = 0; i < numPages; i++) {
    uint32_t offset;
    if (!serialization::readPod(f, offset)) {
      LOG_DBG("TRS", "Cache truncated, rebuilding");
      pageOffsets.clear();
      return false;
    }
    pageOffsets.push_back(offset);
  }

  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Loaded page index cache: %d pages", totalPages);
  return true;
}

void TxtReaderActivity::savePageIndexCache() const {
  std::string cachePath = txt->getCachePath() + "/index.bin";
  HalFile f;
  if (!Storage.openFileForWrite("TRS", cachePath, f)) {
    LOG_ERR("TRS", "Failed to save page index cache");
    return;
  }

  // Write header using serialization module
  serialization::writePod(f, CACHE_MAGIC);
  serialization::writePod(f, CACHE_VERSION);
  serialization::writePod(f, static_cast<uint32_t>(txt->getFileSize()));
  serialization::writePod(f, static_cast<int32_t>(viewportWidth));
  serialization::writePod(f, static_cast<int32_t>(linesPerPage));
  serialization::writePod(f, static_cast<int32_t>(cachedFontId));
  serialization::writePod(f, static_cast<int32_t>(cachedScreenMargin));
  serialization::writePod(f, cachedParagraphAlignment);
  serialization::writePod(f, static_cast<uint32_t>(pageOffsets.size()));

  // Write page offsets
  for (size_t offset : pageOffsets) {
    serialization::writePod(f, static_cast<uint32_t>(offset));
  }

  LOG_DBG("TRS", "Saved page index cache: %d pages", totalPages);
}

int TxtReaderActivity::pagesUntilNextParagraph() const {
  if (paraPagesFor == currentPage) return paraPagesLeft;
  int left = 0;
  if (currentPage + 1 < totalPages) {
    // Scan from the last byte of this page for the first paragraph break: a newline, or
    // the form feed that opens a page. The pages strictly between this one and the page
    // holding that break lie wholly inside the paragraph, the same count the EPUB reader
    // gives. ponytail: capped at 20 pages; a hard-wrapped file (a newline every line)
    // reads as one paragraph per line and so always shows 0.
    constexpr int MAX_PAGES = 20;
    const int lastPage = std::min(totalPages - 1, currentPage + 1 + MAX_PAGES);
    const size_t from = pageOffsets[currentPage + 1] - 1;
    const size_t to = lastPage + 1 < totalPages ? pageOffsets[lastPage + 1] : txt->getFileSize();
    size_t breakAt = to - 1;  // no break: the paragraph runs through lastPage
    HalFile f;
    if (!Storage.openFileForRead("TRS", txt->getPath(), f) || !f.seek(from)) {
      left = -1;
    } else {
      uint8_t buf[128];
      bool found = false;
      for (size_t pos = from; pos < to && !found;) {
        const int n = f.read(buf, std::min(sizeof(buf), to - pos));
        if (n <= 0) break;
        for (int i = 0; i < n && !found; i++) {
          if (buf[i] == '\n' || buf[i] == '\f') {
            // A form feed is a page's first byte: the paragraph ended on the page before.
            breakAt = buf[i] == '\n' ? pos + i : pos + i - 1;
            found = true;
          }
        }
        pos += n;
      }
      const int endPage =
          static_cast<int>(std::upper_bound(pageOffsets.begin(), pageOffsets.end(), breakAt) - pageOffsets.begin()) - 1;
      left = std::max(0, endPage - currentPage - 1);
    }
  }
  paraPagesFor = currentPage;
  paraPagesLeft = left;
  return left;
}
