#pragma once
#include <cstdint>
namespace BoardConfig {
// Only the hardware inputs read by the actual UC8279 X4 driver.
struct BoardProfile {
  uint16_t displayWidth = 800;
  uint16_t displayHeight = 480;
  uint32_t displaySpiHz = 16000000;
  uint8_t displayControllerVariant = 0x02;
};
inline BoardProfile ACTIVE;
}  // namespace BoardConfig
