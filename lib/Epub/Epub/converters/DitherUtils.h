#pragma once

#include <BitmapHelpers.h>
#include <stdint.h>

// 4x4 Bayer matrix for ordered dithering
inline const uint8_t bayer4x4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

// One pixel's 4-level value (0-3) on the display-tuned curve covers use (quantizeGray4).
// Dithered: an ordered Bayer pick between the two panel levels around gray, in proportion
// to where gray sits between them. Stateless, so any pixel order works.
inline uint8_t quantize4Level(const uint8_t gray, const int x, const int y, const bool useDithering) {
  if (!useDithering) return quantizeGray4(gray, Gray4QuantizationMode::DisplayTuned).index;
  const uint8_t* level = DISPLAY_TUNED_GRAY4;
  if (gray <= level[0]) return 0;
  if (gray >= level[3]) return 3;
  const uint8_t low = gray < level[1] ? 0 : gray < level[2] ? 1 : 2;
  // Up a level when (gray - low) / span > (bayer + 0.5) / 16.
  const int span = level[low + 1] - level[low];
  return (gray - level[low]) * 32 > (bayer4x4[y & 3][x & 3] * 2 + 1) * span ? low + 1 : low;
}
