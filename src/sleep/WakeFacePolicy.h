#pragma once
#include <cstdint>

namespace wake_face {
// Persisted IDs must not be renumbered. Retired Dark, Blank, Quick Resume and
// Freeze selections become Light; all other sleep faces keep their identity.
inline constexpr uint8_t migrateSleepScreen(uint8_t mode) {
  return mode == 0 || mode == 5 || mode == 6 || mode == 8 ? 1 : mode;
}
}  // namespace wake_face
