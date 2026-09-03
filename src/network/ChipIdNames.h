#pragma once

#include <cstdint>

namespace firmware_flash {

// Human-readable MCU family name for a given esp_chip_id_t (e.g. "ESP32-C3", "ESP32-S3").
inline const char* chipName(uint16_t chipId) {
  switch (chipId) {
    case 0x0005:
      return "ESP32-C3";
    case 0x0009:
      return "ESP32-S3";
    case 0x0000:
      return "ESP32";
    case 0x0002:
      return "ESP32-S2";
    case 0x000C:
      return "ESP32-C2";
    case 0x000D:
      return "ESP32-C6";
    case 0x0010:
      return "ESP32-H2";
    default:
      return "Unknown";
  }
}

}  // namespace firmware_flash
