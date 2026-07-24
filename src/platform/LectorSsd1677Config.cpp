// Lector's SSD1677 (Xteink X4) panel config: the stock FreeInk X4 waveform values
// with the grayscale rail power-up turned on.
//
// WHY this override exists
// ------------------------
// The X4's HALF update sequence is 0x22 = 0xD7, and bits 0x03 of that byte are
// ANALOG_OFF|CLOCK_OFF, so the panel rails are DOWN when the sequence finishes
// (Ssd1677Driver::refresh tracks this as _isScreenOn = false).
//
// Every grayscale sleep screen paints its base frame with HALF_REFRESH — the .pxc
// wallpaper, the BMP wallpaper and the book-cover sleep screen all do. The custom-LUT
// grayscale refresh that follows therefore finds the rails down and folds
// CLOCK_ON|ANALOG_ON into the same MASTER_ACTIVATION as the grayscale waveform
// (0x22 = 0xCC instead of 0x0C), which is exactly the situation
// Ssd1677Config::grayPowerUpFirst exists to prevent: the LUT phases run while the
// booster is still ramping. On the X4 that stalled the panel BUSY line, freezing the
// device on the "Entering sleep" popup until the SDK's 30 s BUSY ceiling expired.
//
// The SDK leaves grayPowerUpFirst off for the X4 because "the X4 keeps the panel
// powered between fast refreshes". That is true for the FAST-base grayscale callers
// (reader anti-aliasing, XTC comics) but not for the HALF-base sleep screens. Turning
// it on costs one extra power-up activation on the sleep path and is a no-op on the
// fast paths, where powerOn() returns immediately because the panel is already on.
//
// Selected with -DFREEINK_SSD1677_CONFIG=lectorSsd1677Config, set only on the X3/X4
// environments so the Seeed Sticky environment keeps its own SSD1677 config.

#include "driver/Ssd1677Driver.h"

namespace freeink {

namespace {

Ssd1677Config buildLectorX4Config() {
  Ssd1677Config cfg = ssd1677DefaultConfig();
  cfg.grayPowerUpFirst = true;
  return cfg;
}

}  // namespace

const Ssd1677Config& lectorSsd1677Config() {
  static const Ssd1677Config cfg = buildLectorX4Config();
  return cfg;
}

}  // namespace freeink
