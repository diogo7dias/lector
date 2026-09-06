#include "BitmapHelpers.h"

#include <cstdint>
#include <cstring>  // Added for memset

#include "Bitmap.h"

// Simple quantization without dithering - divide into 4 levels
// The thresholds are fine-tuned to the X4 display
uint8_t quantizeSimple(int gray) {
  if (gray < 45) {
    return 0;
  } else if (gray < 70) {
    return 1;
  } else if (gray < 140) {
    return 2;
  } else {
    return 3;
  }
}

// 1-bit noise dithering for fast home screen rendering
// Uses hash-based noise for consistent dithering that works well at small sizes
uint8_t quantize1bit(int gray, int x, int y) {
  // Generate noise threshold using integer hash (no regular pattern to alias)
  uint32_t hash = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
  hash = (hash ^ (hash >> 13)) * 1274126177u;
  const int threshold = static_cast<int>(hash >> 24);  // 0-255

  // Simple threshold with noise: gray >= (128 + noise offset) -> white
  // The noise adds variation around the 128 midpoint
  const int adjustedThreshold = 128 + ((threshold - 128) / 2);  // Range: 64-192
  return (gray >= adjustedThreshold) ? 1 : 0;
}

void createBmpHeader(BmpHeader* bmpHeader, int width, int height, BmpRowOrder rowOrder) {
  if (!bmpHeader) return;

  // Zero out the memory to ensure no garbage data if called on uninitialized stack memory
  std::memset(bmpHeader, 0, sizeof(BmpHeader));

  uint32_t rowSize = (width + 31) / 32 * 4;
  uint32_t imageSize = rowSize * height;
  uint32_t fileSize = sizeof(BmpHeader) + imageSize;

  bmpHeader->fileHeader.bfType = 0x4D42;
  bmpHeader->fileHeader.bfSize = fileSize;
  bmpHeader->fileHeader.bfReserved1 = 0;
  bmpHeader->fileHeader.bfReserved2 = 0;
  bmpHeader->fileHeader.bfOffBits = sizeof(BmpHeader);

  bmpHeader->infoHeader.biSize = sizeof(bmpHeader->infoHeader);
  bmpHeader->infoHeader.biWidth = width;
  bmpHeader->infoHeader.biHeight = (rowOrder == BmpRowOrder::TopDown) ? -height : height;
  bmpHeader->infoHeader.biPlanes = 1;
  bmpHeader->infoHeader.biBitCount = 1;
  bmpHeader->infoHeader.biCompression = 0;
  bmpHeader->infoHeader.biSizeImage = imageSize;
  bmpHeader->infoHeader.biXPelsPerMeter = 2835;  // 72 DPI
  bmpHeader->infoHeader.biYPelsPerMeter = 2835;  // 72 DPI
  bmpHeader->infoHeader.biClrUsed = 2;
  bmpHeader->infoHeader.biClrImportant = 2;

  // Color 0 (black)
  bmpHeader->colors[0].rgbBlue = 0;
  bmpHeader->colors[0].rgbGreen = 0;
  bmpHeader->colors[0].rgbRed = 0;
  bmpHeader->colors[0].rgbReserved = 0;

  // Color 1 (white)
  bmpHeader->colors[1].rgbBlue = 255;
  bmpHeader->colors[1].rgbGreen = 255;
  bmpHeader->colors[1].rgbRed = 255;
  bmpHeader->colors[1].rgbReserved = 0;
}