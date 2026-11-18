#pragma once

#include <HalStorage.h>

class Print;

// First frame of a non-interlaced GIF as the 2-bit BMP the cover and sleep paths read, scaled
// like PngToBmpConverter's (fit, or cover when `crop`).
class GifToBmpConverter {
  static bool gifFileToBmpStreamInternal(HalFile& gifFile, Print& bmpOut, int targetWidth, int targetHeight,
                                         bool crop = true);

 public:
  static bool gifFileToBmpStream(HalFile& gifFile, Print& bmpOut, bool crop = true);
};
