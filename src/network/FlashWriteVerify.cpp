#include "FlashWriteVerify.h"

#include <algorithm>
#include <cstring>
#include <memory>

namespace firmware_flash {

namespace {
constexpr size_t SEC = 4096;       // flash sector: erase granularity
constexpr size_t BLK = 64 * 1024;  // block erase granularity
constexpr size_t CHUNK = 4096;     // read/write/compare chunk
size_t g_mismatchOffset = 0;
}  // namespace

size_t lastVerifyMismatchOffset() { return g_mismatchOffset; }

Result writeImage(ByteSource& src, FlashTarget& dst, ProgressCb onProgress, void* ctx) {
  const size_t imageSize = src.size();
  if (imageSize > dst.size()) return Result::TOO_LARGE;
  if (!src.rewind()) return Result::READ_FAIL;

  auto buffer = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[CHUNK]);
  if (!buffer) return Result::OOM;

  // Interleave erase + write so the progress bar advances 0 to 100% smoothly
  // rather than stalling for several seconds during a single up-front erase.
  size_t streamPos = 0;
  size_t erasedUpto = 0;
  while (streamPos < imageSize) {
    if (streamPos >= erasedUpto) {
      size_t eraseLen = std::min<size_t>(BLK, dst.size() - streamPos);
      eraseLen = (eraseLen + SEC - 1) & ~(SEC - 1);
      eraseLen = std::min<size_t>(eraseLen, dst.size() - streamPos);
      if (!dst.erase(streamPos, eraseLen)) return Result::ERASE_FAIL;
      erasedUpto = streamPos + eraseLen;
    }

    const size_t want = std::min<size_t>(CHUNK, imageSize - streamPos);
    const int got = src.read(buffer.get(), want);
    if (got <= 0 || static_cast<size_t>(got) != want) return Result::READ_FAIL;
    if (!dst.write(streamPos, buffer.get(), want)) return Result::WRITE_FAIL;
    streamPos += want;
    if (onProgress) onProgress(streamPos, imageSize, ctx);
  }
  return Result::OK;
}

Result StreamWriter::write(const uint8_t* data, size_t len) {
  if (pos_ + len > dst_.size()) return Result::TOO_LARGE;

  size_t offset = 0;
  while (offset < len) {
    if (pos_ >= erasedUpto_) {
      size_t eraseLen = std::min<size_t>(BLK, dst_.size() - pos_);
      eraseLen = (eraseLen + SEC - 1) & ~(SEC - 1);
      eraseLen = std::min<size_t>(eraseLen, dst_.size() - pos_);
      if (!dst_.erase(pos_, eraseLen)) return Result::ERASE_FAIL;
      erasedUpto_ = pos_ + eraseLen;
    }
    // Never write past the erased frontier in one call, or the tail of a large
    // chunk would land in a block that has not been erased yet.
    const size_t take = std::min(len - offset, erasedUpto_ - pos_);
    if (!dst_.write(pos_, data + offset, take)) return Result::WRITE_FAIL;
    pos_ += take;
    offset += take;
  }
  return Result::OK;
}

void StreamWriter::restart() {
  pos_ = 0;
  erasedUpto_ = 0;
}

Result verifyImage(ByteSource& src, FlashTarget& dst, ProgressCb onProgress, void* ctx) {
  const size_t imageSize = src.size();
  if (imageSize > dst.size()) return Result::TOO_LARGE;
  if (!src.rewind()) return Result::READ_FAIL;

  auto fromFile = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[CHUNK]);
  auto fromFlash = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[CHUNK]);
  if (!fromFile || !fromFlash) return Result::OOM;

  size_t pos = 0;
  while (pos < imageSize) {
    const size_t want = std::min<size_t>(CHUNK, imageSize - pos);
    const int got = src.read(fromFile.get(), want);
    if (got <= 0 || static_cast<size_t>(got) != want) return Result::READ_FAIL;
    if (!dst.read(pos, fromFlash.get(), want)) return Result::READ_FAIL;
    if (std::memcmp(fromFile.get(), fromFlash.get(), want) != 0) {
      size_t i = 0;
      while (i < want && fromFile[i] == fromFlash[i]) i++;
      g_mismatchOffset = pos + i;
      return Result::VERIFY_FAIL;
    }
    pos += want;
    if (onProgress) onProgress(pos, imageSize, ctx);
  }
  return Result::OK;
}

}  // namespace firmware_flash
