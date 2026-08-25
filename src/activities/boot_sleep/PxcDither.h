#pragma once

#include <cstdint>

#include "Epub/converters/DitherUtils.h"
#include "PxcSleepRenderer.h"

// How a .pxc's two mid levels survive being rendered in one bit.
//
// A .pxc carries four levels. A plain BW pass keeps 0 and 3 and has to decide what to do
// with 1 and 2, and that decision is the whole difference between a light wallpaper that
// reads as a photograph and one that reads as a black blob. The shipped answer is a 2x2
// Bayer mask, which is cheap and visibly regular; the alternatives exist so the Lock Lab
// can measure that regularity against them on the panel rather than argue about it. This
// header is not lab-gated because the shipped path uses it too.
namespace pxcdither {

// The shipped mask. Kept here rather than inline in the decode loop so the release path
// and the lab path are provably the same code with the same numbers.
inline constexpr uint8_t kBayer2[2][2] = {{0, 2}, {3, 1}};

// The 4x4 mask is not defined here: it is the bayer4x4 already used for EPUB images in
// lib/Epub/Epub/converters/DitherUtils.h. Note that rescaling its 0..15 range down to the
// 0..3 used below would collapse it back into exactly kBayer2, so the BAYER4 case instead
// scales the level up into the mask's own 16 steps, which is what makes it a different
// picture rather than a differently written identical one.

// Void-and-cluster blue noise, 16x16, each of the four threshold values used exactly 64
// times. Ordered masks put their error on a lattice, which the eye finds because the eye
// is a lattice detector; blue noise spreads the same error across frequencies the eye is
// least sensitive to. 256 bytes, constexpr, so it lives in flash.
inline constexpr uint8_t kBlue16[16][16] = {
    {2, 0, 2, 1, 1, 0, 3, 2, 1, 3, 3, 2, 3, 0, 0, 3}, {3, 1, 3, 0, 3, 2, 1, 0, 1, 2, 0, 1, 3, 1, 2, 1},
    {0, 1, 2, 2, 3, 1, 3, 0, 3, 0, 2, 0, 2, 0, 3, 2}, {1, 3, 0, 1, 0, 0, 2, 2, 3, 1, 3, 2, 3, 1, 0, 2},
    {3, 2, 1, 3, 3, 1, 2, 1, 0, 2, 0, 1, 0, 3, 2, 0}, {3, 0, 2, 1, 2, 0, 3, 3, 1, 2, 3, 1, 3, 2, 1, 1},
    {2, 1, 0, 3, 1, 3, 0, 1, 0, 3, 1, 2, 0, 0, 3, 0}, {1, 2, 3, 0, 2, 0, 2, 2, 3, 1, 0, 1, 3, 1, 2, 3},
    {0, 2, 1, 2, 3, 1, 2, 1, 0, 2, 3, 2, 2, 0, 2, 0}, {1, 3, 0, 1, 0, 3, 0, 3, 1, 3, 0, 1, 3, 1, 3, 3},
    {0, 2, 3, 2, 0, 1, 2, 0, 2, 1, 2, 0, 2, 0, 1, 2}, {1, 3, 0, 1, 3, 2, 1, 3, 0, 2, 3, 1, 3, 2, 1, 2},
    {0, 1, 2, 2, 0, 1, 0, 3, 1, 0, 0, 1, 3, 0, 1, 3}, {1, 3, 0, 3, 1, 3, 2, 2, 1, 2, 3, 2, 0, 2, 0, 2},
    {0, 2, 2, 1, 0, 3, 0, 0, 3, 2, 0, 3, 2, 1, 3, 3}, {1, 3, 0, 3, 2, 2, 1, 3, 0, 1, 1, 0, 1, 2, 1, 0},
};

// Collapses a mid level (1 or 2) to black or white. Called only for those two levels, so
// pure black and pure white always stay solid whichever mask is chosen. `mode` is a
// literal on the release path, which leaves nothing of the switch behind.
inline uint8_t ditherMid(PxcRenderOptions::Dither mode, uint8_t level, int row, int col) {
  uint8_t threshold;
  switch (mode) {
    case PxcRenderOptions::BAYER4:
      // 16 steps rather than 4: level 1 and level 2 become 7/16 and 12/16 white instead of
      // 1/4 and 2/4, so the mask has somewhere finer to put the error.
      return ((level * 5 + 2) > bayer4x4[row & 3][col & 3]) ? 3 : 0;
    case PxcRenderOptions::BLUE16:
      threshold = kBlue16[row & 15][col & 15];
      break;
    case PxcRenderOptions::THRESHOLD:
      // No mask at all: level 1 goes black, level 2 goes white. The baseline the dithers
      // have to beat, and the fastest of the four.
      return (level >= 2) ? 3 : 0;
    case PxcRenderOptions::BAYER2:
    default:
      threshold = kBayer2[row & 1][col & 1];
      break;
  }
  return (level > threshold) ? 3 : 0;
}

}  // namespace pxcdither
