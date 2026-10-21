#pragma once
#include <cstdint>
// The 792x528 X3 profile; the drivers read nothing else from BoardConfig.
namespace BoardConfig {
struct Profile {
  uint16_t displayWidth = 792;
  uint16_t displayHeight = 528;
  uint32_t displaySpiHz = 0;
};
inline Profile ACTIVE;
}  // namespace BoardConfig
