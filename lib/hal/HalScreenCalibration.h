#pragma once

#include <cstdint>

// How far the bezel covers each panel edge on this one reader: top, right, bottom, left in the
// Portrait frame. In NVS rather than the SD settings, because it belongs to the device and not
// to whichever card is in it.
namespace HalScreenCalibration {
// False when nothing valid is stored, which leaves the board's own measurements in place.
bool load(uint8_t (&edges)[4]);
bool save(const uint8_t (&edges)[4]);
}  // namespace HalScreenCalibration
