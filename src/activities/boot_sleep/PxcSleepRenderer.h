#pragma once
#include <HalDisplay.h>

#include <cstdint>
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

// The knobs the Lock Lab turns. Default-constructed, this struct describes exactly what
// the renderer did before it existed, and every call site that does not care passes
// nullptr, so the release path is unchanged. The lab build (LECTOR_LOCK_LAB) is the only
// thing that ever fills one in; see src/dev/LockLab.h.
struct PxcRenderOptions {
  // How the two mid levels survive a 1-bit render. BAYER2 is what the wallpaper has
  // always used; the rest are here to be compared against it.
  enum Dither : uint8_t { BAYER2 = 0, BAYER4, BLUE16, THRESHOLD };
  // Which of the three grayscale passes actually run, so the cost of the base paint can
  // be told apart from the cost of the two planes.
  enum Passes : uint8_t { THREE = 0, BASE_ONLY, PLANES_ONLY };

  Dither dither = BAYER2;
  Passes passes = THREE;
  // The whole tone curve a 2bpp source can carry: level 0..3 in, level 0..3 out. Identity
  // leaves the image as the encoder made it.
  uint8_t levelMap[4] = {0, 1, 2, 3};
  bool invert = false;
  // Negative means "ask sleepGrayscaleBaseRefresh()", which is the shipped behaviour;
  // otherwise a HalDisplay::RefreshMode to force.
  int8_t grayBaseRefresh = -1;
  // False forces the per-pass row-batch SD reads even when the payload would fit RAM.
  bool wholeFileCache = true;
  // 0 means the shipped ~4KB batch; anything else is that many rows per read.
  uint16_t rowsPerRead = 0;
};

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
// overlay, when set, is called once per render pass, after the wallpaper has been
// decoded into the framebuffer and before that pass is committed, so its drawing
// composites on top of the wallpaper rather than arriving as a second refresh. On the
// 1-bit path that is one call (the wake banners use this); on the grayscale path it is
// three — BW base, LSB plane, MSB plane — because a plane pass only carries the pixels
// written during that pass, exactly like the wallpaper itself. An overlay that should
// stay solid black-and-white must therefore check GfxRenderer::getRenderMode() and draw
// its 1-bit parts only in the BW pass (see SleepInfoOverlay). A plain function pointer,
// not std::function: no heap, no per-signature code bloat.
//
// Decode is on demand (no pre-staging): the payload is read once into RAM when it
// fits, else re-read in small row batches per pass, so it never OOM-bricks at the
// low, fragmented heap of sleep entry.
//
// opts is nullptr everywhere except the Lock Lab, and a nullptr means "behave exactly as
// this function did before the lab existed". The parts of the decode loop it can change
// are compiled out entirely unless LECTOR_LOCK_LAB is defined, so a release build carries
// neither the branches nor the tables.
bool renderPxcSleepScreen(GfxRenderer& renderer, const std::string& path, bool grayscale = true,
                          HalDisplay::RefreshMode oneBitRefresh = HalDisplay::HALF_REFRESH,
                          void (*overlay)(GfxRenderer&) = nullptr, const PxcRenderOptions* opts = nullptr);
