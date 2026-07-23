#pragma once
#include <HalDisplay.h>

#include <string>
#include <string_view>

class GfxRenderer;

// True if fileName ends in ".pxc" (case-insensitive). Kept local to the sleep
// wallpaper path (not in the shared FsHelpers) so upstream CrossPoint merges stay
// conflict-free.
inline bool hasPxcExtension(std::string_view fileName) {
  const size_t n = fileName.size();
  if (n < 4) return false;
  return fileName[n - 4] == '.' && (fileName[n - 3] | 0x20) == 'p' && (fileName[n - 2] | 0x20) == 'x' &&
         (fileName[n - 1] | 0x20) == 'c';
}

// Renders a full-screen (panel-sized, e.g. 480x800) pre-dithered 2-bits-per-pixel
// .pxc wallpaper to the e-ink framebuffer using CrossPoint's 3-pass grayscale
// pipeline (BW base + LSB plane + MSB plane), mirroring
// SleepActivity::renderBitmapSleepScreen but sourcing pixels from a .pxc file.
//
// The .pxc format is the same little-endian-header 2bpp pixel cache CrossPoint
// uses for EPUB images (see converters/PixelCache.h): uint16 width, uint16
// height, then 2bpp packed pixels (4 px/byte, MSB-first, row-major; level 0..3 ->
// gray 0/85/170/255). Files are produced by the Lector Wallpaper Converter and
// authored at the exact panel size, so they draw at the origin with no scaling or
// cropping and must match the screen within +/-1 px. Returns false on open /
// header / size-mismatch / allocation / read failure so the caller can fall
// through to another sleep screen.
//
// grayscale=false renders a single 1-bit (black/white) refresh of the wallpaper,
// skipping the 3-pass grayscale pipeline (faster, for a wake banner). The two mid
// gray levels are ordered-dithered so tone survives in pure B&W. oneBitRefresh
// selects that path's panel refresh (default HALF = clean base); it is ignored on
// the grayscale path.
//
// Decode is on demand (no pre-staging): the payload is read once into RAM when it
// fits, else re-read in small row batches per pass, so it never OOM-bricks at the
// low, fragmented heap of sleep entry.
bool renderPxcSleepScreen(GfxRenderer& renderer, const std::string& path, bool grayscale = true,
                          HalDisplay::RefreshMode oneBitRefresh = HalDisplay::HALF_REFRESH);
