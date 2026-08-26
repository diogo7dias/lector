#pragma once

#include <cstdint>

// What the light panel's Rotate button steps through.
//
// Three of the four orientations, in the order a hand turns a reader: portrait, then the
// panel's native landscape, then the other landscape, then back. Portrait Inverted is
// deliberately absent — it is reachable from the settings row for anyone who wants it,
// but a button that could land there would strand the reader upside down with no
// obvious way back.
//
// Written out rather than computed, because the stored values are not in visual order:
// Portrait 0, Landscape CW 1, Portrait Inverted 2, Landscape CCW 3.
namespace orientation_cycle {

// Values are CrossPointSettings::READER_ORIENTATION. Spelled out here so this stays
// host-testable without the storage layer.
inline uint8_t next(const uint8_t orientation) {
  switch (orientation) {
    case 0:  // Portrait -> Landscape CCW, the panel's own orientation
      return 3;
    case 3:  // Landscape CCW -> Landscape CW
      return 1;
    case 1:  // Landscape CW -> Portrait
      return 0;
    default:
      // Portrait Inverted, or a value no build wrote. Enter the cycle rather than doing
      // nothing, so the press is never dead.
      return 3;
  }
}

}  // namespace orientation_cycle
