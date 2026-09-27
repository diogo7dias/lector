#pragma once

#include <cstdint>
#include <cstring>

// Helper functions
uint8_t quantizeSimple(int gray);
uint8_t quantize1bit(int gray, int x, int y);

struct QuantizedGray4 {
  uint8_t index;
  uint8_t value;
};

enum class Gray4QuantizationMode : uint8_t { DisplayTuned, Native };

inline QuantizedGray4 quantizeGray4(int gray, const Gray4QuantizationMode mode) {
  if (gray < 0) gray = 0;
  if (gray > 255) gray = 255;

  if (mode == Gray4QuantizationMode::Native) {
    // Midpoints between the display's four logical levels: 0, 85, 170, 255.
    if (gray < 43) return {0, 0};
    if (gray < 128) return {1, 85};
    if (gray < 213) return {2, 170};
    return {3, 255};
  }

  // Existing thresholds tuned for the X4 panel.
  if (gray < 30) return {0, 15};
  if (gray < 50) return {1, 30};
  if (gray < 140) return {2, 80};
  return {3, 210};
}

// 1-bit Atkinson dithering - better quality than noise dithering for thumbnails
// Error distribution pattern (same as 2-bit but quantizes to 2 levels):
//     X  1/8 1/8
// 1/8 1/8 1/8
//     1/8
class Atkinson1BitDitherer {
 public:
  explicit Atkinson1BitDitherer(int width) : width(width) {
    errorRow0 = new int16_t[width + 4]();  // Current row
    errorRow1 = new int16_t[width + 4]();  // Next row
    errorRow2 = new int16_t[width + 4]();  // Row after next
  }

  ~Atkinson1BitDitherer() {
    delete[] errorRow0;
    delete[] errorRow1;
    delete[] errorRow2;
  }

  // EXPLICITLY DELETE THE COPY CONSTRUCTOR
  Atkinson1BitDitherer(const Atkinson1BitDitherer& other) = delete;

  // EXPLICITLY DELETE THE COPY ASSIGNMENT OPERATOR
  Atkinson1BitDitherer& operator=(const Atkinson1BitDitherer& other) = delete;

  uint8_t processPixel(int gray, int x) {
    // Add accumulated error
    int adjusted = gray + errorRow0[x + 2];
    if (adjusted < 0) adjusted = 0;
    if (adjusted > 255) adjusted = 255;

    // Quantize to 2 levels (1-bit): 0 = black, 1 = white
    uint8_t quantized;
    int quantizedValue;
    if (adjusted < 128) {
      quantized = 0;
      quantizedValue = 0;
    } else {
      quantized = 1;
      quantizedValue = 255;
    }

    // Calculate error (only distribute 6/8 = 75%)
    int error = (adjusted - quantizedValue) >> 3;  // error/8

    // Distribute 1/8 to each of 6 neighbors
    errorRow0[x + 3] += error;  // Right
    errorRow0[x + 4] += error;  // Right+1
    errorRow1[x + 1] += error;  // Bottom-left
    errorRow1[x + 2] += error;  // Bottom
    errorRow1[x + 3] += error;  // Bottom-right
    errorRow2[x + 2] += error;  // Two rows down

    return quantized;
  }

  void nextRow() {
    int16_t* temp = errorRow0;
    errorRow0 = errorRow1;
    errorRow1 = errorRow2;
    errorRow2 = temp;
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

  void reset() {
    memset(errorRow0, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow1, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

 private:
  int width;
  int16_t* errorRow0;
  int16_t* errorRow1;
  int16_t* errorRow2;
};

// Atkinson dithering - distributes only 6/8 (75%) of error for cleaner results
// Error distribution pattern:
//     X  1/8 1/8
// 1/8 1/8 1/8
//     1/8
// Less error buildup = fewer artifacts than Floyd-Steinberg
class AtkinsonDitherer {
 public:
  explicit AtkinsonDitherer(int width, Gray4QuantizationMode quantizationMode = Gray4QuantizationMode::DisplayTuned)
      : width(width), quantizationMode(quantizationMode) {
    errorRow0 = new int16_t[width + 4]();  // Current row
    errorRow1 = new int16_t[width + 4]();  // Next row
    errorRow2 = new int16_t[width + 4]();  // Row after next
  }

  ~AtkinsonDitherer() {
    delete[] errorRow0;
    delete[] errorRow1;
    delete[] errorRow2;
  }
  // **1. EXPLICITLY DELETE THE COPY CONSTRUCTOR**
  AtkinsonDitherer(const AtkinsonDitherer& other) = delete;

  // **2. EXPLICITLY DELETE THE COPY ASSIGNMENT OPERATOR**
  AtkinsonDitherer& operator=(const AtkinsonDitherer& other) = delete;

  uint8_t processPixel(int gray, int x) {
    // Add accumulated error
    int adjusted = gray + errorRow0[x + 2];
    if (adjusted < 0) adjusted = 0;
    if (adjusted > 255) adjusted = 255;

    const QuantizedGray4 quantized = quantizeGray4(adjusted, quantizationMode);

    // Calculate error (only distribute 6/8 = 75%)
    int error = (adjusted - quantized.value) >> 3;  // error/8

    // Distribute 1/8 to each of 6 neighbors
    errorRow0[x + 3] += error;  // Right
    errorRow0[x + 4] += error;  // Right+1
    errorRow1[x + 1] += error;  // Bottom-left
    errorRow1[x + 2] += error;  // Bottom
    errorRow1[x + 3] += error;  // Bottom-right
    errorRow2[x + 2] += error;  // Two rows down

    return quantized.index;
  }

  void nextRow() {
    int16_t* temp = errorRow0;
    errorRow0 = errorRow1;
    errorRow1 = errorRow2;
    errorRow2 = temp;
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

  void reset() {
    memset(errorRow0, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow1, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

 private:
  int width;
  Gray4QuantizationMode quantizationMode;
  int16_t* errorRow0;
  int16_t* errorRow1;
  int16_t* errorRow2;
};

// Output size for a src image scaled to fit inside target (crop = false) or to
// cover it (crop = true), aspect kept, each side at least 1 pixel. Shared by
// the JPEG and PNG cover converters.
inline void scaleToFit(const int srcWidth, const int srcHeight, const int targetWidth, const int targetHeight,
                       const bool crop, int& outWidth, int& outHeight) {
  const float scaleToFitWidth = static_cast<float>(targetWidth) / static_cast<float>(srcWidth);
  const float scaleToFitHeight = static_cast<float>(targetHeight) / static_cast<float>(srcHeight);
  float scale;
  if (crop) {
    scale = (scaleToFitWidth > scaleToFitHeight) ? scaleToFitWidth : scaleToFitHeight;
  } else {
    scale = (scaleToFitWidth < scaleToFitHeight) ? scaleToFitWidth : scaleToFitHeight;
  }

  outWidth = static_cast<int>(static_cast<float>(srcWidth) * scale);
  outHeight = static_cast<int>(static_cast<float>(srcHeight) * scale);
  if (outWidth < 1) outWidth = 1;
  if (outHeight < 1) outHeight = 1;
}

// Area averaging on the X axis: adds one source row into the per-output-column
// sums and counts. CountT is the caller's counter width.
template <typename CountT>
void accumulateAreaRow(const uint8_t* srcRow, const int srcWidth, const int outWidth, const uint32_t scaleX_fp,
                       uint32_t* rowAccum, CountT* rowCount) {
  for (int outX = 0; outX < outWidth; outX++) {
    const int srcXStart = (static_cast<uint32_t>(outX) * scaleX_fp) >> 16;
    const int srcXEnd = (static_cast<uint32_t>(outX + 1) * scaleX_fp) >> 16;
    int sum = 0;
    int count = 0;
    for (int srcX = srcXStart; srcX < srcXEnd && srcX < srcWidth; srcX++) {
      sum += srcRow[srcX];
      count++;
    }
    if (count == 0 && srcXStart < srcWidth) {
      sum = srcRow[srcXStart];
      count = 1;
    }
    rowAccum[outX] += sum;
    rowCount[outX] += count;
  }
}

// Dithers (or, without a ditherer, quantizes) one row of gray values into a
// packed 1-bit or 2-bit BMP row. grayAt(x) yields the gray for column x.
template <typename GrayAt>
void packBmpRow(uint8_t* bmpRow, const int bytesPerRow, const int width, const int y, const bool oneBit,
                Atkinson1BitDitherer* ditherer1Bit, AtkinsonDitherer* ditherer2Bit, GrayAt grayAt) {
  memset(bmpRow, 0, bytesPerRow);

  if (oneBit) {
    for (int x = 0; x < width; x++) {
      const uint8_t gray = grayAt(x);
      const uint8_t bit = ditherer1Bit ? ditherer1Bit->processPixel(gray, x) : quantize1bit(gray, x, y);
      bmpRow[x / 8] |= (bit << (7 - (x % 8)));
    }
    if (ditherer1Bit) ditherer1Bit->nextRow();
  } else {
    for (int x = 0; x < width; x++) {
      const uint8_t gray = grayAt(x);
      const uint8_t twoBit = ditherer2Bit ? ditherer2Bit->processPixel(gray, x) : quantizeSimple(gray);
      bmpRow[(x * 2) / 8] |= (twoBit << (6 - ((x * 2) % 8)));
    }
    if (ditherer2Bit) ditherer2Bit->nextRow();
  }
}
