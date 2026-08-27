#pragma once

#include <cstdint>

// What the light panel's Rotate button steps through.
//
// Two orientations: portrait and the clockwise landscape. The other landscape and
// Portrait Inverted are both reachable from the settings row and neither belongs on a
// button — inverted strands the reader upside down, and a second landscape made Rotate a
// three-press round trip back to the page you were already reading.
//
// Written out rather than computed, because the stored values are not in visual order:
// Portrait 0, Landscape CW 1, Portrait Inverted 2, Landscape CCW 3.
namespace orientation_cycle {

// Values are CrossPointSettings::READER_ORIENTATION. Spelled out here so this stays
// host-testable without the storage layer.
inline uint8_t next(const uint8_t orientation) {
  // Landscape CW is the only value that leads anywhere but portrait: every orientation
  // outside the cycle enters it at portrait rather than making the press dead.
  return orientation == 0 ? 1 : 0;
}

}  // namespace orientation_cycle
