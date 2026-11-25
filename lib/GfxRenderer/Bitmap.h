#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <cstdlib>
#include <memory>

#include "BitmapHelpers.h"

enum class BmpReaderError : uint8_t {
  Ok = 0,
  FileInvalid,
  SeekStartFailed,

  NotBMP,
  DIBTooSmall,

  BadPlanes,
  UnsupportedBpp,
  UnsupportedCompression,

  BadDimensions,
  ImageTooLarge,
  PaletteTooLarge,

  SeekPixelDataFailed,
  BufferTooSmall,
  OomRowBuffer,
  ShortReadRow,
};

enum class BitmapToneMapping : uint8_t { None, Adaptive };

class Bitmap {
 public:
  explicit Bitmap(HalFile& file, bool dithering = false, BitmapToneMapping toneMapping = BitmapToneMapping::None)
      : file(file), dithering(dithering), toneMapping(toneMapping) {}
  BmpReaderError parseHeaders();
  BmpReaderError readNextRow(uint8_t* data, uint8_t* rowBuffer) const;
  BmpReaderError rewindToData() const;
  // Shrink a dithered high-color image that is larger than maxWidth x maxHeight to fit
  // inside it, aspect kept, while decoding: source pixels are area-averaged, then
  // dithered on the output grid. Dithering at source size and letting the renderer drop
  // pixels aliases the dot pattern into seams and loses thin lines. False (size
  // unchanged) when it does not apply or the resampler cannot be allocated.
  bool ditherToFit(int maxWidth, int maxHeight);
  // X4 Pro only (PSRAM): keep every decoded row of the first full pass so rewindToData()
  // replays them instead of reading and dithering the file again; a grey sleep face draws
  // the same picture three times. Call after parseHeaders()/ditherToFit() and before the
  // first row. False, and the file path stays as it is, without PSRAM or on OOM.
  bool cacheRows();
  // The size rows come out at: the source size unless ditherToFit changed it.
  int getWidth() const { return outputWidth; }
  int getHeight() const { return outputHeight; }
  bool isTopDown() const { return topDown; }
  bool hasGreyscale() const { return bpp > 1; }
  int getRowBytes() const { return rowBytes; }
  bool is1Bit() const { return bpp == 1; }
  uint16_t getBpp() const { return bpp; }

 private:
  static uint16_t readLE16(HalFile& f);
  static uint32_t readLE32(HalFile& f);
  bool analyzeAdaptiveToneMapping();
  uint8_t applyAdaptiveTone(uint8_t luminance) const;
  uint8_t pixelLuminance(const uint8_t* row, int x) const;
  BmpReaderError readAveragedRow(uint8_t* rowBuffer) const;

  HalFile& file;
  bool dithering = false;
  BitmapToneMapping toneMapping = BitmapToneMapping::None;
  int width = 0;
  int height = 0;
  bool topDown = false;
  uint32_t bfOffBits = 0;
  uint16_t bpp = 0;
  uint32_t colorsUsed = 0;
  bool nativePalette = false;  // true if all palette entries map to native gray levels
  int rowBytes = 0;
  int outputWidth = 0;
  int outputHeight = 0;
  Gray4QuantizationMode ditherMode = Gray4QuantizationMode::DisplayTuned;
  uint8_t paletteLum[256] = {};
  bool adaptiveToneMapping = false;
  uint8_t adaptiveBlackPoint = 0;
  uint8_t adaptiveWhitePoint = 255;

  // Dithering state (mutable for const methods)
  mutable std::unique_ptr<AtkinsonDitherer> atkinsonDitherer;
  // Resampling state, set only by ditherToFit: one sum per output column.
  std::unique_ptr<uint32_t[]> resampleSums;
  mutable int sourceRowsRead = 0;
  mutable int outputRowsRead = 0;

  struct FreeBytes {
    void operator()(uint8_t* p) const { free(p); }  // heap_caps_free is free() on ESP-IDF
  };
  std::unique_ptr<uint8_t[], FreeBytes> rowCache;
  mutable int cachedRows = 0;
  mutable bool replaying = false;
};
