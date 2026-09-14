#pragma once
#include <cstdint>

namespace wake_face {
// Persisted IDs must not be renumbered. Retired Dark, Blank, Quick Resume and
// Freeze selections become Light; all other sleep faces keep their identity.
inline constexpr uint8_t migrateSleepScreen(uint8_t mode) {
  return mode == 0 || mode == 5 || mode == 6 || mode == 8 ? 1 : mode;
}

// How long the wake waits, measured from gpio.begin(), before the recovery chord is
// read. The X3/X4 buttons are an ADC resistor ladder that shares a supply with the
// panel rails; the X4 Pro's are debounced digital inputs. Fast Unlock cuts the ladder
// window from 500 ms to 100 ms: the card mount, the settings load and the display
// bring-up already run inside the window, and the chord itself is still confirmed by
// two agreeing samples 6 ms apart. Chosen from code inspection, hence the setting.
inline constexpr unsigned long inputSettleMs(const bool isX4Pro, const bool fastUnlock) {
  if (isX4Pro) return 20;
  return fastUnlock ? 100 : 500;
}
}  // namespace wake_face
