#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Pure otadata-record logic, free of any ESP header so it can be unit tested on
// the host. Layout reference: esp_flash_partitions.h.

namespace ota_boot {

struct __attribute__((packed)) SelectEntry {
  uint32_t ota_seq;
  uint8_t seq_label[20];
  uint32_t ota_state;
  uint32_t crc;
};
static_assert(sizeof(SelectEntry) == 32, "SelectEntry must be 32 bytes");

constexpr uint32_t kOtaImgNew = 0;              // ESP_OTA_IMG_NEW
constexpr uint32_t kOtaImgPendingVerify = 1;    // ESP_OTA_IMG_PENDING_VERIFY
constexpr uint32_t kOtaImgInvalid = 3;          // ESP_OTA_IMG_INVALID
constexpr uint32_t kOtaImgAborted = 4;          // ESP_OTA_IMG_ABORTED
constexpr uint32_t kOtaImgUndefined = 0xFFFFFFFFu;  // ESP_OTA_IMG_UNDEFINED
constexpr size_t kOtaSeqCrcLen = 4;

// Smallest sequence number that both outranks `activeSeq` and makes the
// bootloader select OTA slot `destOtaIdx`, which it picks with
// (seq - 1) % numOtaParts.
inline uint32_t nextSeqFor(uint32_t activeSeq, uint32_t destOtaIdx, uint32_t numOtaParts) {
  uint32_t seq = activeSeq + 1;
  while (((seq - 1u) % numOtaParts) != (destOtaIdx % numOtaParts)) ++seq;
  return seq;
}

// Build the otadata record for `seq`.
//
// ota_state is UNDEFINED, not NEW, on purpose. A NEW record turns into
// PENDING_VERIFY on the next boot, and the bootloader then rolls the device
// back to the other slot unless the freshly booted app calls
// esp_ota_mark_app_valid_cancel_rollback(). Lector gets that call for free from
// the Arduino core, but the stock and CrossInk builds a user may flash from the
// SD card do not, so their first boot was silently rolled back into Lector and
// the device looked stuck on Lector forever. UNDEFINED leaves rollback
// disarmed, so the flashed image keeps running. Integrity is not weakened:
// firmware_flash::validateImageFile checks header, segment table, XOR checksum
// and SHA256 before any of these bytes are written.
inline SelectEntry makeSelectEntry(uint32_t seq, uint32_t crc) {
  SelectEntry entry = {};
  entry.ota_seq = seq;
  std::memset(entry.seq_label, 0xFF, sizeof(entry.seq_label));
  entry.ota_state = kOtaImgUndefined;
  entry.crc = crc;
  return entry;
}

}  // namespace ota_boot
