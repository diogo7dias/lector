#pragma once

#include <esp_partition.h>

#include <cstdint>

#include "OtaBootEntry.h"

// X4 (and X3) factory bootloaders accept our patch_firmware_image.py-patched
// firmware.bin (web flasher proves this), but the running ESP-IDF's
// esp_image_verify rejects with bogus efuse-blk-rev errors. Both SD-card and
// OTA update paths bypass that runtime check by writing the OTA app partition
// raw and updating otadata directly — same scheme as the web flasher
// (crosspoint-reader-docs/src/lib/flasher/OtaPartition.ts).
//
// The otadata record itself is built in OtaBootEntry.h, which is host-testable.

namespace ota_boot {

// CRC32-LE over the 4-byte ota_seq, init UINT32_MAX. Matches IDF and web flasher.
uint32_t computeSeqCrc(uint32_t seq);

// Switch the bootloader's selected app partition to `dest` by writing a fresh
// otadata entry into the inactive otadata slot. Bypasses esp_ota_set_boot_partition's
// esp_image_verify call. The bytes in `dest` must already be a valid app image
// (e.g. patch_firmware_image.py output) — caller is responsible for that.
//
// Returns true on success.
bool switchTo(const esp_partition_t* dest);

}  // namespace ota_boot
