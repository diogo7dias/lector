#pragma once
#include <cstdint>
namespace BoardConfig {
struct SdmmcPins {
  int8_t clk = -1, cmd = -1, d0 = -1, d1 = -1, d2 = -1, d3 = -1;
};
struct Profile {
  SdmmcPins sdmmc;
};
inline const Profile ACTIVE{};
}  // namespace BoardConfig
