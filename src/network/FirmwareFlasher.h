#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

// Flash a firmware image from an SD-card path into the next OTA app
// partition, then switch otadata so the X3/X4 stock bootloader picks it up
// on next boot. Mirrors the web flasher: raw esp_partition_erase_range +
// esp_partition_write + ota_boot::switchTo (no Arduino Update class, no
// esp_image_verify — those reject our patched image on X4 silicon).
//
// Both the SD update activity and the OTA path land here. The SD path streams
// from a file (flashFromSdPath); the OTA path streams from the network
// (StreamingInstall), because an HTTP body cannot be rewound and there is
// nowhere on the device to spool 6 MB other than the destination partition.

namespace firmware_flash {

enum class Result {
  OK,
  OPEN_FAIL,
  TOO_SMALL,
  TOO_LARGE,
  BAD_MAGIC,
  BAD_SEGMENTS,  // segment table malformed or runs past EOF
  BAD_CHECKSUM,  // ESP image XOR checksum mismatch
  BAD_SHA,       // SHA256 trailer mismatch (hash_appended images)
  BAD_CHIP,      // image chip_id doesn't match the running MCU family
  BAD_SIZE,      // body+pad+sha length doesn't match file size
  NO_PARTITION,
  OOM,
  READ_FAIL,
  ERASE_FAIL,
  WRITE_FAIL,
  OTADATA_FAIL,
  VERIFY_FAIL,  // flash readback does not match the source image
};

// Progress callback: called after every chunk write. `written`/`total` are bytes.
using ProgressCb = void (*)(size_t written, size_t total, void* ctx);

// Open `sdPath`, validate it looks like an ESP32 image, then stream it into the
// next OTA app partition with interleaved 64 KiB erase + sector writes. On
// success switches otadata via ota_boot::switchTo. Caller is responsible for
// ESP.restart() afterwards.
//
// `alreadyValidated` lets callers that have just run `validateImageFile()`
// themselves (e.g. SdFirmwareUpdateActivity, which validates before showing
// the user the confirmation prompt) skip the redundant second pass. Defaults
// to false so callers without prior validation (any future entry point) keep
// the defense-in-depth check.
// After the write, every byte is read back out of the partition and compared
// against the file; otadata is switched only when they match. A bad write is
// otherwise invisible: the bootloader refuses the image, falls back to the
// slot Lector runs from, and leaves otadata pointing at the rejected copy, so
// the device reports a successful update and comes back on the old firmware
// on every boot from then on.
//
// `onVerifyProgress` (optional) reports that readback pass, which streams the
// same number of bytes again and so takes about as long as the write.
Result flashFromSdPath(const char* sdPath, ProgressCb onProgress, void* ctx, bool alreadyValidated = false,
                       ProgressCb onVerifyProgress = nullptr);

// Full-image integrity check that mirrors the bootloader's verification:
// header magic, segment table walk, XOR checksum, and SHA256 trailer (when
// hash_appended == 1). Run this before flashing a candidate firmware so a
// truncated/corrupted .bin never reaches otadata.
//
// `partitionSize` is the size of the destination OTA partition; pass 0 to
// skip the size-fits-partition check (e.g. when validating ahead of partition
// lookup). Streams the file in CHUNK-sized reads; the file is rewound on
// success so the caller can immediately reread it for flashing.
Result validateImageFile(const char* sdPath, size_t partitionSize);

struct ByteSource;

// Same integrity check as validateImageFile, over any rewindable stream.
Result validateImageStream(ByteSource& src, size_t partitionSize);

// Installs an image that arrives in chunks the caller does not control, e.g.
// an OTA download. Bytes land in the next OTA app partition as they come;
// commit() validates them where they landed and only then switches otadata.
//
// commit() switches via ota_boot::switchTo, which leaves the bootloader's
// rollback disarmed, so a firmware that never calls
// esp_ota_mark_app_valid_cancel_rollback() (stock Xteink, CrossPoint,
// CrossInk, INX -- anything not built on the Arduino core) survives its first
// boot instead of being rolled straight back into lector.
class StreamingInstall {
 public:
  StreamingInstall();
  ~StreamingInstall();
  StreamingInstall(const StreamingInstall&) = delete;
  StreamingInstall& operator=(const StreamingInstall&) = delete;

  // Resolve the destination partition. NO_PARTITION when there is none.
  Result begin();
  // Append the next chunk of the image.
  Result feed(const uint8_t* data, size_t len);
  // Discard everything written and start over at offset 0, for a server that
  // ignored a Range request and replayed the body.
  void restart();
  // Validate what is in flash, then hand the device to it on the next boot.
  Result commit();

  size_t written() const;
  size_t capacity() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

const char* resultName(Result r);

// Returns the chip_id (esp_image_header_t offset 12) of the currently-running
// image, or 0xFFFF if it cannot be read. Because the running slot booted
// successfully, its chip_id is authoritative for the current CPU, so a
// candidate image must match it to be safe to flash.
uint16_t runningPartitionChipId();

}  // namespace firmware_flash
