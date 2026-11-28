#pragma once
#include <cstdint>

namespace BoardConfig {
enum class Board : uint8_t { XteinkX4, XteinkX4Pro, Sticky };
struct Orientation {
  bool mirrorX = false;
  bool mirrorY = false;
};
struct Profile {
  Board board = Board::XteinkX4Pro;
  uint16_t displayWidth = 800;
  uint16_t displayHeight = 480;
  uint32_t displaySpiHz = 0;
  Orientation orientation{};
};
inline Profile ACTIVE;
}  // namespace BoardConfig
