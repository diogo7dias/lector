#pragma once

#include <cstddef>
#include <cstdint>

#include "FirmwareFlasher.h"

// Partition-agnostic half of the firmware flasher: stream an image into a
// flash target, then read every byte back and compare it against the source.
//
// The readback matters because a bad write is otherwise invisible. The stock
// bootloader validates the image it is pointed at, and when that check fails
// it silently falls back to the other app slot and leaves otadata alone — so a
// single corrupted byte turns into "the update ran, and the device came back
// on the old firmware", on every boot, with no error anywhere.
//
// Both interfaces are abstract so the write/erase/verify logic can run on the
// host against a fake NOR flash; the ESP-side adapters live in
// FirmwareFlasher.cpp.

namespace firmware_flash {

// A rewindable stream of image bytes (an SD-card file in production).
struct ByteSource {
  virtual ~ByteSource() = default;
  virtual size_t size() const = 0;
  // Seek back to byte 0. Returns false if the stream cannot be re-read.
  virtual bool rewind() = 0;
  // Returns bytes read, or a negative value on error. A short read is an error
  // to the caller; only `len` bytes are ever requested within the image.
  virtual int read(uint8_t* dst, size_t len) = 0;
};

// The destination app partition. Offsets are relative to the partition start.
struct FlashTarget {
  virtual ~FlashTarget() = default;
  virtual size_t size() const = 0;
  virtual bool erase(size_t offset, size_t len) = 0;
  virtual bool write(size_t offset, const uint8_t* src, size_t len) = 0;
  virtual bool read(size_t offset, uint8_t* dst, size_t len) = 0;
};

// Erase-ahead in 64 KiB blocks, write in 4 KiB chunks, reporting progress after
// every chunk. Rewinds `src` first, so callers may pass a stream they have
// already validated.
Result writeImage(ByteSource& src, FlashTarget& dst, ProgressCb onProgress, void* ctx);

// Re-read the whole image from `dst` and compare it against `src`. Returns
// VERIFY_FAIL on the first mismatching byte. Reports progress the same way the
// write does, since the readback streams the same number of bytes.
Result verifyImage(ByteSource& src, FlashTarget& dst, ProgressCb onProgress = nullptr, void* ctx = nullptr);

// Push-mode counterpart of writeImage, for an image that arrives from the
// network: the caller cannot rewind an HTTP body, and the chunk boundaries are
// the transport's, not ours. Same interleaved erase-ahead, same 64 KiB blocks.
//
// There is no readback pass here. The image is validated by reading it back out
// of the partition afterwards (firmware_flash::validateFlashedImage), which
// checks the bytes the bootloader will actually see and so covers a bad write
// as well as a bad download.
class StreamWriter {
 public:
  explicit StreamWriter(FlashTarget& dst) : dst_(dst) {}

  // Append `len` bytes at the current offset, erasing ahead as needed.
  Result write(const uint8_t* data, size_t len);

  // Drop everything written so far and start again at offset 0. Used when a
  // server ignores our Range header and replays the whole body, which would
  // otherwise write a second copy of the image on top of the first.
  void restart();

  size_t written() const { return pos_; }

 private:
  FlashTarget& dst_;
  size_t pos_ = 0;
  size_t erasedUpto_ = 0;
};

// Byte offset of the mismatch reported by the last verifyImage() failure, for
// logging. Undefined after a successful verify.
size_t lastVerifyMismatchOffset();

}  // namespace firmware_flash
