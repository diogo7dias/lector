// The transparent sleep overlay: finding one, checking it and drawing it (SleepOverlay.h).
// Ported from CrossPoint 2.0.0. Split out of SleepActivity.cpp to keep each file under the
// token limit.
#include "SleepOverlay.h"

#include <Bitmap.h>
#include <Epub/converters/PngToFramebufferConverter.h>
#include <FsHelpers.h>
#include <Logging.h>
#include <Memory.h>
#include <PNGdec.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "CrossPointSettings.h"

namespace {

// Kept apart from /sleep so alpha art never mixes with full-screen wallpapers.
constexpr char OVERLAY_ROOT_BMP[] = "/sleep-overlay.bmp";
constexpr char OVERLAY_ROOT_PNG[] = "/sleep-overlay.png";
constexpr char OVERLAY_DIR[] = "/.sleep-overlay";
constexpr char OVERLAY_LEGACY_DIR[] = "/sleep-overlay";
constexpr size_t MAX_NAME_LEN = 256;
constexpr uint8_t MIN_VISIBLE_ALPHA = 8;

uint16_t readLE16(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  return static_cast<uint16_t>((c0 < 0 ? 0 : c0) | ((c1 < 0 ? 0 : c1) << 8));
}

uint32_t readLE32(HalFile& file) {
  const uint32_t lo = readLE16(file);
  return lo | (static_cast<uint32_t>(readLE16(file)) << 16);
}

uint32_t readBE32(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const int c2 = file.read();
  const int c3 = file.read();
  if (c0 < 0 || c1 < 0 || c2 < 0 || c3 < 0) return 0;
  return (static_cast<uint32_t>(c0) << 24) | (static_cast<uint32_t>(c1) << 16) | (static_cast<uint32_t>(c2) << 8) |
         static_cast<uint32_t>(c3);
}

// A PNG the decoder will accept, read off the IHDR without decoding.
bool isValidPngHeader(HalFile& file) {
  static constexpr uint8_t PNG_SIGNATURE[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  static constexpr uint32_t MAX_SOURCE_PIXELS = 2048u * 1536u;
  uint8_t signature[8];
  if (!file.seek(0) || file.read(signature, sizeof(signature)) != static_cast<int>(sizeof(signature)) ||
      memcmp(signature, PNG_SIGNATURE, sizeof(signature)) != 0) {
    return false;
  }
  const uint32_t ihdrLength = readBE32(file);
  char chunkType[4];
  if (file.read(reinterpret_cast<uint8_t*>(chunkType), sizeof(chunkType)) != static_cast<int>(sizeof(chunkType)) ||
      ihdrLength != 13 || memcmp(chunkType, "IHDR", 4) != 0) {
    return false;
  }
  const uint32_t width = readBE32(file);
  const uint32_t height = readBE32(file);
  const int bitDepth = file.read();
  const int colorType = file.read();
  const int compression = file.read();
  const int filter = file.read();
  const int interlace = file.read();

  const bool supportedBitDepth =
      bitDepth == 8 || ((colorType == PNG_PIXEL_GRAYSCALE || colorType == PNG_PIXEL_INDEXED) &&
                        (bitDepth == 1 || bitDepth == 2 || bitDepth == 4));
  const bool supportedColorType = colorType == PNG_PIXEL_GRAYSCALE || colorType == PNG_PIXEL_TRUECOLOR ||
                                  colorType == PNG_PIXEL_INDEXED || colorType == PNG_PIXEL_GRAY_ALPHA ||
                                  colorType == PNG_PIXEL_TRUECOLOR_ALPHA;
  return width > 0 && height > 0 && width <= 2048 && height <= 3072 && width * height <= MAX_SOURCE_PIXELS &&
         supportedBitDepth && supportedColorType && compression == 0 && filter == 0 && interlace == 0;
}

// Where an overlay lands: centred, shrunk to fit, or cropped to fill under Crop.
template <typename P>
P placementFor(const int w, const int h, const GfxRenderer& renderer) {
  P p;
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  if (w > pageWidth || h > pageHeight) {
    float ratio = static_cast<float>(w) / static_cast<float>(h);
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);
    const bool crop = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;
    if (ratio > screenRatio) {
      if (crop) {
        p.cropX = 1.0f - (screenRatio / ratio);
        ratio = (1.0f - p.cropX) * static_cast<float>(w) / static_cast<float>(h);
      }
      p.y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
    } else {
      if (crop) {
        p.cropY = 1.0f - (ratio / screenRatio);
        ratio = static_cast<float>(w) / ((1.0f - p.cropY) * static_cast<float>(h));
      }
      p.x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
    }
  } else {
    p.x = (pageWidth - w) / 2;
    p.y = (pageHeight - h) / 2;
  }
  return p;
}

template <typename B>
bool parseOverlayBmpHeader(HalFile& file, B& info) {
  if (!file || !file.seek(0)) return false;
  if (readLE16(file) != 0x4D42) return false;
  file.seekCur(8);
  info.dataOffset = readLE32(file);
  if (readLE32(file) < 40) return false;  // DIB header size
  info.width = static_cast<int32_t>(readLE32(file));
  const auto rawHeight = static_cast<int32_t>(readLE32(file));
  if (rawHeight == std::numeric_limits<int32_t>::min()) return false;
  info.topDown = rawHeight < 0;
  info.height = info.topDown ? -rawHeight : rawHeight;
  const uint16_t planes = readLE16(file);
  const uint16_t bpp = readLE16(file);
  const uint32_t compression = readLE32(file);
  // Only the byte layout of a 32-bit BGRA BMP is read; masks are not parsed.
  if (planes != 1 || bpp != 32 || !(compression == 0 || compression == 3)) return false;
  if (info.width <= 0 || info.height <= 0 || info.width > 2048 || info.height > 3072) return false;
  info.rowBytes = static_cast<uint32_t>(info.width) * 4u;
  return file.seek(info.dataOffset);
}

uint8_t bayerThreshold4x4(const int x, const int y) {
  static constexpr uint8_t BAYER_4X4[16] = {0, 128, 32, 160, 192, 64, 224, 96, 48, 176, 16, 144, 240, 112, 208, 80};
  return BAYER_4X4[((y & 0x03) << 2) | (x & 0x03)];
}

// 0 black, 1 dark grey, 2 light grey, 3 white. The BW pass paints every level but white
// black. A grey plane sets the bit of the levels it carries (drawPixel(false) sets one,
// drawPixel(true) clears one) and clears it for the rest, so an overlay pixel also replaces
// the grey of a cover under it.
void drawOverlayPixel(const GfxRenderer& renderer, const int x, const int y, const uint8_t level,
                      const GfxRenderer::RenderMode pass) {
  switch (pass) {
    case GfxRenderer::GRAYSCALE_LSB:
      renderer.drawPixel(x, y, level != 1);
      break;
    case GfxRenderer::GRAYSCALE_MSB:
      renderer.drawPixel(x, y, level != 1 && level != 2);
      break;
    default:
      renderer.drawPixel(x, y, level < 3);
  }
}

template <typename B, typename P>
bool renderOverlayBmpPass(HalFile& file, const B& info, const P& placement, const GfxRenderer& renderer, uint8_t* row,
                          const GfxRenderer::RenderMode pass) {
  if (!file.seek(info.dataOffset)) return false;
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int cropPixX = std::floor(info.width * placement.cropX / 2.0f);
  const int cropPixY = std::floor(info.height * placement.cropY / 2.0f);
  const float croppedWidth = (1.0f - placement.cropX) * static_cast<float>(info.width);
  const float croppedHeight = (1.0f - placement.cropY) * static_cast<float>(info.height);

  float scale = 1.0f;
  if (croppedWidth > 0.0f && croppedHeight > 0.0f) {
    scale =
        std::min({1.0f, static_cast<float>(pageWidth) / croppedWidth, static_cast<float>(pageHeight) / croppedHeight});
  }
  const bool isScaled = scale < 1.0f;

  for (int bmpY = 0; bmpY < info.height; bmpY++) {
    if (file.read(row, info.rowBytes) != static_cast<int>(info.rowBytes)) {
      LOG_ERR("SLP", "Short read in overlay row %d", bmpY);
      return false;
    }
    int screenY = -cropPixY + (info.topDown ? bmpY : info.height - 1 - bmpY);
    if (isScaled) screenY = std::floor(screenY * scale);
    screenY += placement.y;
    if (screenY >= pageHeight) {
      if (info.topDown) break;
      continue;
    }
    if (screenY < 0) {
      if (!info.topDown) break;
      continue;
    }
    for (int bmpX = cropPixX; bmpX < info.width - cropPixX; bmpX++) {
      int screenX = bmpX - cropPixX;
      if (isScaled) screenX = std::floor(screenX * scale);
      screenX += placement.x;
      if (screenX >= pageWidth) break;
      if (screenX < 0) continue;
      const uint8_t* pixel = row + (static_cast<size_t>(bmpX) * 4u);
      const uint8_t alpha = pixel[3];
      // Ordered dither on the alpha: a half-transparent pixel is drawn about half the time.
      if (alpha < MIN_VISIBLE_ALPHA || alpha <= bayerThreshold4x4(screenX, screenY)) continue;
      const uint8_t lum = (77u * pixel[2] + 150u * pixel[1] + 29u * pixel[0]) >> 8;
      drawOverlayPixel(renderer, screenX, screenY, lum >> 6, pass);
    }
  }
  return true;
}

enum class AlphaScan : uint8_t { Useful, NotUseful, Error };

// An overlay is worth compositing only when it has both visible and see-through pixels; an
// opaque 32-bit BMP is a wallpaper and goes through the plain bitmap path.
template <typename B>
AlphaScan scanForUsefulAlpha(HalFile& file, const B& info, uint8_t* row) {
  if (!file.seek(info.dataOffset)) return AlphaScan::Error;
  bool hasVisiblePixel = false;
  bool hasNonOpaquePixel = false;
  for (int bmpY = 0; bmpY < info.height; bmpY++) {
    if (file.read(row, info.rowBytes) != static_cast<int>(info.rowBytes)) return AlphaScan::Error;
    for (int bmpX = 0; bmpX < info.width; bmpX++) {
      const uint8_t alpha = row[static_cast<size_t>(bmpX) * 4u + 3u];
      hasVisiblePixel |= alpha >= MIN_VISIBLE_ALPHA;
      hasNonOpaquePixel |= alpha < 255;
      if (hasVisiblePixel && hasNonOpaquePixel) return AlphaScan::Useful;
    }
  }
  return AlphaScan::NotUseful;
}

// Counts the usable overlay images in `dir` (rewound first), or names the `pick`-th one.
// name must hold MAX_NAME_LEN bytes.
bool nextOverlayName(HalFile& dir, char* name) {
  for (auto f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (f.isDirectory()) continue;
    f.getName(name, MAX_NAME_LEN);
    if (name[0] == '\0' || name[0] == '.') continue;
    const bool isBmp = FsHelpers::hasBmpExtension(name);
    if (!isBmp && !FsHelpers::hasPngExtension(std::string_view{name})) continue;
    if (isBmp) {
      Bitmap bitmap(f);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) return true;
    } else if (isValidPngHeader(f)) {
      return true;
    }
  }
  return false;
}

// ponytail: a plain random pick over both folders, so the same overlay can come twice in a
// row. CrossPoint walks a no-repeat cycle kept in its state file; add that if repeats annoy.
bool pickOverlay(std::string& path) {
  auto name = makeUniqueNoThrow<char[]>(MAX_NAME_LEN);
  if (!name) {
    LOG_ERR("SLP", "OOM: overlay filename buffer");
    return false;
  }
  const char* const dirs[] = {OVERLAY_DIR, OVERLAY_LEGACY_DIR};
  uint16_t counts[2] = {};
  for (int i = 0; i < 2; i++) {
    auto dir = Storage.open(dirs[i]);
    if (!dir || !dir.isDirectory()) continue;
    while (counts[i] < UINT16_MAX / 2 && nextOverlayName(dir, name.get())) counts[i]++;
  }
  const uint32_t total = counts[0] + counts[1];
  if (total == 0) return false;
  uint32_t index = static_cast<uint32_t>(random(total));
  const int which = index < counts[0] ? 0 : 1;
  if (which == 1) index -= counts[0];
  auto dir = Storage.open(dirs[which]);
  if (!dir || !dir.isDirectory()) return false;
  for (uint32_t i = 0; i <= index; i++) {
    if (!nextOverlayName(dir, name.get())) return false;
  }
  path = std::string(dirs[which]) + "/" + name.get();
  return true;
}

}  // namespace

bool SleepOverlay::prepareFile(const GfxRenderer& renderer, const std::string& candidate) {
  file = HalFile();
  row.reset();
  path = candidate;
  isPng = FsHelpers::hasPngExtension(path);
  if (isPng) {
    ImageDimensions dims;
    if (!PngToFramebufferConverter::getDimensionsStatic(path, dims)) return false;
    placement = placementFor<Placement>(dims.width, dims.height, renderer);
    config = RenderConfig{};
    config.x = placement.x;
    config.y = placement.y;
    config.maxWidth = renderer.getScreenWidth();
    config.maxHeight = renderer.getScreenHeight();
    config.useDithering = false;
    config.sourceCropX = placement.cropX;
    config.sourceCropY = placement.cropY;
    config.useExactDimensions = placement.cropX > 0.0f || placement.cropY > 0.0f;
    config.preserveAlpha = true;
    LOG_DBG("SLP", "Overlay PNG %s (%dx%d)", path.c_str(), dims.width, dims.height);
    return true;
  }
  if (!Storage.openFileForRead("SLP", path, file)) return false;
  if (!parseOverlayBmpHeader(file, bmp)) return false;
  placement = placementFor<Placement>(bmp.width, bmp.height, renderer);
  row = makeUniqueNoThrow<uint8_t[]>(bmp.rowBytes);
  if (!row) {
    LOG_ERR("SLP", "OOM: overlay row (%u bytes)", static_cast<unsigned>(bmp.rowBytes));
    return false;
  }
  if (scanForUsefulAlpha(file, bmp, row.get()) != AlphaScan::Useful) return false;
  LOG_DBG("SLP", "Overlay BMP %s (%dx%d)", path.c_str(), bmp.width, bmp.height);
  return true;
}

bool SleepOverlay::prepare(const GfxRenderer& renderer) {
  if (prepareFile(renderer, OVERLAY_ROOT_BMP) || prepareFile(renderer, OVERLAY_ROOT_PNG)) return true;
  std::string picked;
  if (pickOverlay(picked) && prepareFile(renderer, picked)) return true;
  LOG_ERR("SLP", "No valid transparent sleep overlay found");
  return false;
}

bool SleepOverlay::draw(GfxRenderer& renderer) {
  if (isPng) {
    PngToFramebufferConverter converter;
    return converter.decodeToFramebuffer(path, renderer, config);
  }
  return renderOverlayBmpPass(file, bmp, placement, renderer, row.get(), renderer.getRenderMode());
}
