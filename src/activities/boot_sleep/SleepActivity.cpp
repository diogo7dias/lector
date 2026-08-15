#include "SleepActivity.h"

#include <Epub.h>
#include <Epub/converters/PngToFramebufferConverter.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Txt.h>
#include <Xtc.h>
#include <esp_random.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "PxcSleepRenderer.h"
#include "RecentBooksStore.h"
#include "SleepInfoOverlay.h"
#include "StatsDashboardPolicy.h"
#include "StatsDashboardRenderer.h"
#include "activities/reader/ReaderUtils.h"
#include "components/BannerStyle.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/Logo120.h"
#include "images/MoonIcon.h"
#include "reading_stats/ReaderStatsSession.h"
#include "reading_stats/ReadingStatsClock.h"
#include "reading_stats/ReadingStatsStore.h"
#include "reading_stats/SdStatsFiles.h"
#include "sleep/DirSlotProbe.h"
#include "sleep/SleepWallpaperIndexStore.h"
#include "sleep/WallpaperNames.h"
#include "util/DeferredFavorite.h"
#include "util/FavoriteImageNames.h"
#include "util/TaskWatchdog.h"

static_assert(CrossPointSettings::SLEEP_SCREEN_MODE::STATS_DASHBOARD == stats_dashboard::kStatsDashboardMode);

namespace {

std::string fileNameFromPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

RecentBook recentBookForPath(const std::string& path) {
  const auto& books = RECENT_BOOKS.getBooks();
  const auto found =
      std::find_if(books.begin(), books.end(), [&](const RecentBook& book) { return book.path == path; });
  return found == books.end() ? RecentBook{path, fileNameFromPath(path), "", ""} : *found;
}

// A FAT directory is a flat array of fixed-size slots, so a random wallpaper can be
// reached by SEEKING to a random slot rather than walking every entry (the slot
// probing itself — entryExistsAt, liveSlotCount — lives in sleep/DirSlotProbe.h,
// shared with the wallpaper index reconcile).
//
// Cost is ~2*log2(entries) probes (roughly 25 reads for a 4000-file folder) instead of
// one read per file. The old full walk took seconds on a large folder and could wedge
// sleep entry outright.
//
// Fairness: a file consumes one slot per 13 characters of long name, plus one, so a
// plain random slot favours long-named files in proportion to their name length, and a
// forward walk from the landing slot compounds it by giving each file the dead slots
// before it. The jump below removes both effects with rejection sampling: a landing is
// accepted only when it is the FIRST slot of a wallpaper's entry, so every wallpaper has
// exactly one accepting slot regardless of how many it spans. Rejections re-roll, and
// after MAX_JUMP_TRIES the caller's reservoir walk — already exactly uniform — takes
// over, so the pick is fair either way.
//
// How many landings to try before deferring to the caller's full uniform walk. Accept
// probability is (wallpapers / slots), so a folder of long-named files rejects more
// often; eight tries keeps the fallback rare without making sleep entry slow.
constexpr int MAX_JUMP_TRIES = 8;

using crosspoint::sleep::DIR_SLOT_BYTES;
using crosspoint::sleep::entryExistsAt;
using crosspoint::sleep::isWallpaperName;
using crosspoint::sleep::liveSlotCount;

// True when `offset` is the first slot of the entry a seek there returns.
//
// A long name spans several slots, and seeking into the middle of that chain still
// returns the same entry — which is exactly what makes a plain random slot favour long
// names. Probing the slot before the landing separates the two cases without decoding
// the chain: if it yields the same entry, the landing was inside the chain, not at its
// start. Deriving the chain length from the name instead would be wrong, because a name
// that fits 8.3 has no long-name slots at all.
bool landsOnEntryStart(HalFile& dir, const size_t offset, const char* name) {
  if (offset == 0) return true;  // nothing can precede the first slot
  if (!dir.seekSet(offset - DIR_SLOT_BYTES)) return false;
  auto probe = dir.openNextFile();
  if (!probe) return false;
  char previous[256];  // FAT long-file-name maximum (255 characters plus terminator)
  probe.getName(previous, sizeof(previous));
  probe.close();
  return strcmp(previous, name) != 0;
}

// Random wallpaper name, or empty when the jump could not resolve one (caller falls
// back to the full walk). `dir` is left at an arbitrary position.
std::string pickWallpaperByJump(HalFile& dir) {
  const uint32_t slots = static_cast<uint32_t>(liveSlotCount(dir));
  if (slots == 0) return {};

  char name[256];  // FAT long-file-name maximum (255 characters plus terminator)
  for (int tries = 0; tries < MAX_JUMP_TRIES; tries++) {
    const size_t start = static_cast<size_t>(random(static_cast<long>(slots))) * DIR_SLOT_BYTES;
    if (!dir.seekSet(start)) continue;
    auto entry = dir.openNextFile();
    if (!entry) continue;  // landed past the last entry
    const bool isDir = entry.isDirectory();
    entry.getName(name, sizeof(name));
    entry.close();
    // Re-roll rather than walking forward: a forward walk is what hands each file the
    // dead and non-wallpaper slots ahead of it.
    if (isDir || !isWallpaperName(name)) continue;
    if (!landsOnEntryStart(dir, start, name)) continue;
    return std::string(name);
  }
  return {};
}

// ---------------------------------------------------------------------------
// Transparent sleep overlays (upstream #2937).
//
// An overlay is a 32-bit BGRA BMP whose alpha channel says which pixels paint.
// It is composited over whatever the panel is already holding rather than
// replacing it, so the mode deliberately skips the blank-and-clean pass every
// other wallpaper face runs through.
//
// Kept BMP-only on purpose: .pxc is already quantised to four opaque levels
// when the converter writes it and carries no alpha, so it cannot express an
// overlay. isWallpaperName() therefore does not apply here.
// ---------------------------------------------------------------------------

// Kept separate from /sleep.pxc, /sleep.bmp and /.sleep so alpha art never mixes
// into the full-screen wallpaper rotation (and never reaches the wallpaper index).
constexpr char TRANSPARENT_SLEEP_ROOT_BMP[] = "/sleep-overlay.bmp";
constexpr char TRANSPARENT_SLEEP_ROOT_PNG[] = "/sleep-overlay.png";
constexpr char TRANSPARENT_SLEEP_DIR[] = "/.sleep-overlay";
constexpr char TRANSPARENT_SLEEP_LEGACY_DIR[] = "/sleep-overlay";
constexpr uint8_t MIN_VISIBLE_ALPHA = 8;

struct BitmapPlacement {
  int x = 0;
  int y = 0;
  float cropX = 0.0f;
  float cropY = 0.0f;
};

struct OverlayBmpInfo {
  int width = 0;
  int height = 0;
  bool topDown = false;
  uint32_t dataOffset = 0;
  uint32_t rowBytes = 0;
};

uint16_t readLE16(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const auto b0 = static_cast<uint8_t>(c0 < 0 ? 0 : c0);
  const auto b1 = static_cast<uint8_t>(c1 < 0 ? 0 : c1);
  return static_cast<uint16_t>(b0) | static_cast<uint16_t>(b1 << 8);
}

uint32_t readLE32(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const int c2 = file.read();
  const int c3 = file.read();
  const auto b0 = static_cast<uint8_t>(c0 < 0 ? 0 : c0);
  const auto b1 = static_cast<uint8_t>(c1 < 0 ? 0 : c1);
  const auto b2 = static_cast<uint8_t>(c2 < 0 ? 0 : c2);
  const auto b3 = static_cast<uint8_t>(c3 < 0 ? 0 : c3);
  return static_cast<uint32_t>(b0) | (static_cast<uint32_t>(b1) << 8) | (static_cast<uint32_t>(b2) << 16) |
         (static_cast<uint32_t>(b3) << 24);
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

// PNG colour-type codes, fixed by the PNG specification. Spelled out here rather than
// pulled from <PNGdec.h>: that header drags in zlib's zutil.h, which does not survive
// this translation unit's include set, and five spec constants are not worth the tangle.
constexpr int PNG_COLOR_GRAYSCALE = 0;
constexpr int PNG_COLOR_TRUECOLOR = 2;
constexpr int PNG_COLOR_INDEXED = 3;
constexpr int PNG_COLOR_GRAY_ALPHA = 4;
constexpr int PNG_COLOR_TRUECOLOR_ALPHA = 6;

bool isValidPngHeader(HalFile& file) {
  static constexpr uint8_t PNG_SIGNATURE[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  static constexpr uint32_t MAX_SOURCE_PIXELS = 2048u * 1536u;
  uint8_t signature[8];
  if (!file.seek(0) || file.read(signature, sizeof(signature)) != static_cast<int>(sizeof(signature)) ||
      !std::equal(std::begin(signature), std::end(signature), std::begin(PNG_SIGNATURE))) {
    return false;
  }

  const uint32_t ihdrLength = readBE32(file);
  char chunkType[4];
  if (file.read(reinterpret_cast<uint8_t*>(chunkType), sizeof(chunkType)) != static_cast<int>(sizeof(chunkType)) ||
      ihdrLength != 13 || !std::equal(std::begin(chunkType), std::end(chunkType), "IHDR")) {
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
      bitDepth == 8 || ((colorType == PNG_COLOR_GRAYSCALE || colorType == PNG_COLOR_INDEXED) &&
                        (bitDepth == 1 || bitDepth == 2 || bitDepth == 4));
  const bool supportedColorType = colorType == PNG_COLOR_GRAYSCALE || colorType == PNG_COLOR_TRUECOLOR ||
                                  colorType == PNG_COLOR_INDEXED || colorType == PNG_COLOR_GRAY_ALPHA ||
                                  colorType == PNG_COLOR_TRUECOLOR_ALPHA;
  return width > 0 && height > 0 && width <= 2048 && height <= 3072 && width * height <= MAX_SOURCE_PIXELS &&
         supportedBitDepth && supportedColorType && compression == 0 && filter == 0 && interlace == 0;
}

// Where a bitmap lands on the panel: centred, scaled down to fit, and cropped to
// fill when the user picked CROP. Factored out of renderBitmapSleepScreen so the
// overlay compositor below places its art exactly like every other sleep bitmap.
BitmapPlacement calculateBitmapPlacement(const int bitmapWidth, const int bitmapHeight, const GfxRenderer& renderer) {
  BitmapPlacement placement;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (bitmapWidth > pageWidth || bitmapHeight > pageHeight) {
    float ratio = static_cast<float>(bitmapWidth) / static_cast<float>(bitmapHeight);
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    if (ratio > screenRatio) {
      // Wider than the viewport: centre the scaled image vertically.
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        placement.cropX = 1.0f - (screenRatio / ratio);
        ratio = (1.0f - placement.cropX) * static_cast<float>(bitmapWidth) / static_cast<float>(bitmapHeight);
      }
      placement.x = 0;
      placement.y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
    } else {
      // Taller than the viewport: centre the scaled image horizontally.
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        placement.cropY = 1.0f - (ratio / screenRatio);
        ratio = static_cast<float>(bitmapWidth) / ((1.0f - placement.cropY) * static_cast<float>(bitmapHeight));
      }
      placement.x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      placement.y = 0;
    }
  } else {
    placement.x = (pageWidth - bitmapWidth) / 2;
    placement.y = (pageHeight - bitmapHeight) / 2;
  }

  return placement;
}

bool parseOverlayBmpHeader(HalFile& file, OverlayBmpInfo& info, const bool logErrors) {
  if (!file) return false;
  if (!file.seek(0)) return false;

  if (readLE16(file) != 0x4D42) {
    if (logErrors) LOG_ERR("SLP", "Transparent overlay is not a BMP");
    return false;
  }

  file.seekCur(8);
  info.dataOffset = readLE32(file);

  const uint32_t dibSize = readLE32(file);
  if (dibSize < 40) {
    if (logErrors) LOG_ERR("SLP", "Unsupported BMP DIB header: %u", static_cast<unsigned>(dibSize));
    return false;
  }

  info.width = static_cast<int32_t>(readLE32(file));
  const auto rawHeight = static_cast<int32_t>(readLE32(file));
  if (rawHeight == std::numeric_limits<int32_t>::min()) {
    if (logErrors) LOG_ERR("SLP", "Bad transparent overlay dimensions: %dx%d", info.width, rawHeight);
    return false;
  }
  info.topDown = rawHeight < 0;
  info.height = info.topDown ? -rawHeight : rawHeight;

  const uint16_t planes = readLE16(file);
  const uint16_t bpp = readLE16(file);
  const uint32_t compression = readLE32(file);

  // Match Bitmap::parseHeaders(): accept BI_RGB (0) and 32bpp BI_BITFIELDS (3), but keep the same
  // byte-layout assumption as custom sleep BMPs. The renderer below treats pixels as BGRA and does not parse masks.
  if (planes != 1 || bpp != 32 || !(compression == 0 || compression == 3)) {
    if (logErrors) {
      LOG_ERR("SLP", "Transparent overlay must be 32-bit BGRA BMP (planes=%u bpp=%u comp=%u)", planes, bpp,
              static_cast<unsigned>(compression));
    }
    return false;
  }

  constexpr int MAX_IMAGE_WIDTH = 2048;
  constexpr int MAX_IMAGE_HEIGHT = 3072;
  if (info.width <= 0 || info.height <= 0 || info.width > MAX_IMAGE_WIDTH || info.height > MAX_IMAGE_HEIGHT) {
    if (logErrors) LOG_ERR("SLP", "Bad transparent overlay dimensions: %dx%d", info.width, info.height);
    return false;
  }

  info.rowBytes = static_cast<uint32_t>(info.width) * 4u;
  if (!file.seek(info.dataOffset)) {
    if (logErrors) LOG_ERR("SLP", "Failed to seek transparent overlay pixel data");
    return false;
  }

  return true;
}

uint8_t bayerThreshold4x4(const int x, const int y) {
  static constexpr uint8_t BAYER_4X4[16] = {0, 128, 32, 160, 192, 64, 224, 96, 48, 176, 16, 144, 240, 112, 208, 80};
  return BAYER_4X4[((y & 0x03) << 2) | (x & 0x03)];
}

enum class TransparentOverlayPass : uint8_t { BW, GrayscaleLsb, GrayscaleMsb };

uint8_t quantizeOverlayLum(const uint8_t lum) {
  // Match Bitmap's native-palette path: 0, 85, 170, 255 map directly to levels 0..3.
  return lum >> 6;
}

bool renderTransparentOverlayPass(HalFile& file, const OverlayBmpInfo& info, const BitmapPlacement& placement,
                                  const GfxRenderer& renderer, uint8_t* row, const TransparentOverlayPass pass) {
  if (!file.seek(info.dataOffset)) {
    LOG_ERR("SLP", "Failed to seek transparent overlay pixel data");
    return false;
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int cropPixX = std::floor(info.width * placement.cropX / 2.0f);
  const int cropPixY = std::floor(info.height * placement.cropY / 2.0f);
  const float croppedWidth = (1.0f - placement.cropX) * static_cast<float>(info.width);
  const float croppedHeight = (1.0f - placement.cropY) * static_cast<float>(info.height);

  float scale = 1.0f;
  if (croppedWidth > 0.0f && croppedHeight > 0.0f) {
    const float widthScale = static_cast<float>(pageWidth) / croppedWidth;
    const float heightScale = static_cast<float>(pageHeight) / croppedHeight;
    scale = std::min(widthScale, heightScale);
    if (scale > 1.0f) scale = 1.0f;
  }
  const bool isScaled = scale < 1.0f;

  for (int bmpY = 0; bmpY < info.height; bmpY++) {
    // Same reason as the wallpaper folder walk above: this runs inline on the loop
    // task while going to sleep, and a tall overlay is thousands of row reads.
    if ((bmpY & 0x3F) == 0) {
      resetTaskWatchdogIfSubscribed();
      vTaskDelay(1);
    }

    if (file.read(row, info.rowBytes) != static_cast<int>(info.rowBytes)) {
      LOG_ERR("SLP", "Short read in transparent overlay row %d", bmpY);
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
      // Partial alpha has nowhere to go on a 1-bit panel, so an ordered dither
      // decides per pixel whether it paints at all. That turns a soft edge into
      // a stipple instead of a hard cut.
      if (alpha < MIN_VISIBLE_ALPHA || alpha <= bayerThreshold4x4(screenX, screenY)) continue;

      const uint8_t lum = (77u * pixel[2] + 150u * pixel[1] + 29u * pixel[0]) >> 8;
      const uint8_t level = quantizeOverlayLum(lum);

      switch (pass) {
        case TransparentOverlayPass::BW:
          // Same first pass as custom bitmap sleep: all non-white levels are painted black.
          // Transparent overlay's only difference is that opaque white explicitly erases underlying text.
          renderer.drawPixel(screenX, screenY, level < 3);
          break;
        case TransparentOverlayPass::GrayscaleLsb:
          if (level == 1) renderer.drawPixel(screenX, screenY, false);
          break;
        case TransparentOverlayPass::GrayscaleMsb:
          if (level == 1 || level == 2) renderer.drawPixel(screenX, screenY, false);
          break;
      }
    }
  }

  return true;
}

enum class AlphaOverlayResult : uint8_t { Rendered, NotAlphaOverlay, Error };
enum class AlphaScanResult : uint8_t { Useful, NotUseful, Error };

// A 32-bit BMP is only worth the alpha path if something is actually visible AND
// something is actually see-through. A fully opaque 32-bit image is just a wallpaper,
// and is better served by the ordinary bitmap path with its tone mapping.
AlphaScanResult scanForUsefulAlpha(HalFile& file, const OverlayBmpInfo& info, uint8_t* row) {
  if (!file.seek(info.dataOffset)) {
    LOG_ERR("SLP", "Failed to seek transparent overlay pixel data");
    return AlphaScanResult::Error;
  }

  bool hasVisiblePixel = false;
  bool hasNonOpaquePixel = false;
  for (int bmpY = 0; bmpY < info.height; bmpY++) {
    if ((bmpY & 0x3F) == 0) {
      resetTaskWatchdogIfSubscribed();
      vTaskDelay(1);
    }

    if (file.read(row, info.rowBytes) != static_cast<int>(info.rowBytes)) {
      LOG_ERR("SLP", "Short read while checking transparent overlay row %d", bmpY);
      return AlphaScanResult::Error;
    }

    for (int bmpX = 0; bmpX < info.width; bmpX++) {
      const uint8_t alpha = row[static_cast<size_t>(bmpX) * 4u + 3u];
      hasVisiblePixel |= alpha >= MIN_VISIBLE_ALPHA;
      hasNonOpaquePixel |= alpha < 255;
      if (hasVisiblePixel && hasNonOpaquePixel) return AlphaScanResult::Useful;
    }
  }

  return AlphaScanResult::NotUseful;
}

AlphaOverlayResult tryRenderTransparentOverlayBmp(HalFile& file, GfxRenderer& renderer, const char* pathForLog) {
  OverlayBmpInfo info;
  if (!parseOverlayBmpHeader(file, info, false)) return AlphaOverlayResult::NotAlphaOverlay;

  const auto placement = calculateBitmapPlacement(info.width, info.height, renderer);
  auto row = makeUniqueNoThrow<uint8_t[]>(info.rowBytes);
  if (!row) {
    LOG_ERR("SLP", "OOM: transparent overlay row (%u bytes)", static_cast<unsigned>(info.rowBytes));
    return AlphaOverlayResult::Error;
  }

  const auto alphaScanResult = scanForUsefulAlpha(file, info, row.get());
  if (alphaScanResult == AlphaScanResult::Error) return AlphaOverlayResult::Error;
  if (alphaScanResult == AlphaScanResult::NotUseful) return AlphaOverlayResult::NotAlphaOverlay;

  LOG_DBG("SLP", "Rendering transparent overlay: %s (%dx%d)", pathForLog, info.width, info.height);

  if (!renderTransparentOverlayPass(file, info, placement, renderer, row.get(), TransparentOverlayPass::BW))
    return AlphaOverlayResult::Error;
  // Must stay HALF for the same reason renderBitmapSleepScreen documents: the gray
  // nudge LUT is calibrated against the state the HALF waveform leaves behind.
  renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  if (!renderTransparentOverlayPass(file, info, placement, renderer, row.get(), TransparentOverlayPass::GrayscaleLsb)) {
    renderer.setRenderMode(GfxRenderer::BW);
    // The BW composite is already on the panel. Keep it instead of falling
    // through to another overlay with this grayscale work buffer cleared.
    return AlphaOverlayResult::Rendered;
  }
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  if (!renderTransparentOverlayPass(file, info, placement, renderer, row.get(), TransparentOverlayPass::GrayscaleMsb)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return AlphaOverlayResult::Rendered;
  }
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  return AlphaOverlayResult::Rendered;
}

// Reservoir sample of the overlay folder: keep the k-th candidate with probability
// 1/k, so every file is equally likely in one pass and O(1) memory. Deliberately NOT
// the wallpaper index — overlays are a separate, small pool, and indexing them would
// put alpha art into the wallpaper rotation.
//
// Uniform each sleep, with no no-repeat rule. The rotation's no-repeat machinery hangs
// off APP_STATE.lastSleepWallpaperPath, and an overlay must not be written there: that
// field is what tells the wake it is holding a wallpaper, and what the reader menu
// offers to favourite, pause or delete. Overlays are none of those things. Upstream
// (#2937) instead added a second 16-entry recency ring to state.json; that is a lot of
// persisted state and an extra SD write per sleep for a folder that typically holds a
// handful of files.
std::string pickOverlayFromDir(const char* dirPath) {
  auto dir = Storage.open(dirPath);
  if (!dir || !dir.isDirectory()) return {};

  const std::string prefix = std::string(dirPath) + "/";
  std::string chosen;
  uint32_t seen = 0;
  uint32_t scanned = 0;
  char name[256];  // FAT long-file-name maximum (255 chars + terminator)

  for (auto dirFile = dir.openNextFile(); dirFile; dirFile = dir.openNextFile()) {
    if ((++scanned & 0x3F) == 0) {
      resetTaskWatchdogIfSubscribed();
      vTaskDelay(1);
    }
    if (dirFile.isDirectory()) {
      dirFile.close();
      continue;
    }
    dirFile.getName(name, sizeof(name));
    if (name[0] == '\0' || name[0] == '.') {
      dirFile.close();
      continue;
    }

    const bool isBmp = FsHelpers::hasBmpExtension(name);
    const bool isPng = FsHelpers::hasPngExtension(std::string_view{name});
    if (!isBmp && !isPng) {
      dirFile.close();
      continue;
    }

    // Headers are checked here, unlike the wallpaper folder walk above which only
    // looks at names. Exactly one file gets rendered per sleep, so an unreadable
    // candidate would cost the user the whole face and drop them to the default
    // logo screen. An overlay folder holds a handful of files, so the per-file
    // header read is affordable; a wallpaper folder holds thousands and is not.
    const bool isValid = isBmp ? Bitmap(dirFile).parseHeaders() == BmpReaderError::Ok : isValidPngHeader(dirFile);
    dirFile.close();
    if (!isValid) {
      LOG_DBG("SLP", "Skipping invalid sleep overlay: %s", name);
      continue;
    }

    ++seen;
    if (random(static_cast<long>(seen)) == 0) chosen = name;
  }
  dir.close();

  return chosen.empty() ? std::string() : prefix + chosen;
}

// Shows the "Entering sleep" popup without leaving it in the framebuffer, so a
// transparent overlay still composites onto the clean page underneath.
//
// Upstream (#2974) computes the band from popup metrics this fork does not have: its
// popup is a centred dialog, lector's is a banner strip pinned to the top edge. The
// geometry here mirrors BaseTheme::drawBannerStrip — black backing from physical row 0
// down through the viewable inset, then PAD + line + PAD, with the rule inside that.
bool drawSleepPopupPreservingFrame(GfxRenderer& renderer) {
  int viewTop = 0, viewRight = 0, viewBottom = 0, viewLeft = 0;
  renderer.getOrientedViewableTRBL(&viewTop, &viewRight, &viewBottom, &viewLeft);
  const int bandTop = 0;
  const int bandHeight =
      std::min(renderer.getScreenHeight(), viewTop + banner::PAD * 2 + renderer.getLineHeight(banner::FONT_ID));
  if (bandHeight <= 0) return false;
  const size_t bandBytes = renderer.getRegionByteSize(0, bandTop, renderer.getScreenWidth(), bandHeight);

  auto savedBand = makeUniqueNoThrow<uint8_t[]>(bandBytes);
  if (!savedBand) {
    LOG_ERR("SLP", "OOM: sleep popup background (%u bytes)", static_cast<unsigned>(bandBytes));
    return false;
  }
  if (!renderer.copyRegionToBuffer(0, bandTop, renderer.getScreenWidth(), bandHeight, savedBand.get(), bandBytes)) {
    LOG_ERR("SLP", "Failed to save sleep popup background");
    return false;
  }

  GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
  if (!renderer.copyBufferToRegion(0, bandTop, renderer.getScreenWidth(), bandHeight, savedBand.get(), bandBytes)) {
    LOG_ERR("SLP", "Failed to restore sleep popup background");
    return false;
  }
  return true;
}

}  // namespace

void SleepActivity::onEnter() {
  Activity::onEnter();

  // Sleep screens always use normal polarity. This activity draws directly
  // from onEnter (outside ActivityManager's per-render polarity resolution),
  // so clear any inversion left over from a night-mode reader render.
  const bool frameWasInverted = display.isInverted();
  display.setInverted(false);

  // Every other sleep face repaints the whole panel, so dropping the inversion costs
  // it nothing. A transparent overlay keeps the framebuffer that is already up, so the
  // inversion has to be baked into the pixels first or the retained page flips to
  // black-on-white the moment the driver stops inverting on output.
  if (frameWasInverted && SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::TRANSPARENT_CUSTOM) {
    renderer.invertScreen();
  }

  // Run and land any queued favourite rename before the folder is touched and before the
  // state below is written. The renames DELIBERATELY start here, not at the press: on a
  // FAT card every name operation is a linear directory scan — seconds with thousands of
  // wallpapers — and each scan holds the storage mutex against the reader's page turns.
  // The lock is the moment the user has stopped reading, so the seconds belong here.
  // Deep sleep is a chip reset: a rename still sitting in the queue would be lost, while
  // the name it was promised could already have been saved to the card. Bounded so a
  // jammed worker can never block sleeping; sized for a few scan-heavy renames.
  DeferredFavorite::waitForIdle(15000);
  DeferredFavorite::reconcile();

  // Deep sleep is a chip reset, so the wake cannot know what the panel is holding unless
  // we write it down. Clear first and let the render path set it, so any screen that is
  // not a wallpaper leaves it empty. Saved only when it changed: a fixed /sleep.pxc gives
  // the same value every sleep and must not cost an SD write each time.
  previousWallpaper = APP_STATE.lastSleepWallpaperPath;
  APP_STATE.lastSleepWallpaperPath.clear();

  renderSleepScreen();

  if (APP_STATE.lastSleepWallpaperPath != previousWallpaper || stateDirty) {
    APP_STATE.saveToFile();
  }
}

void SleepActivity::renderSleepScreen() const {
  const bool renderQuickResume =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
      (fromTimeout &&
       SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT);

  if (renderQuickResume) {
    return renderLastScreenSleepScreen();
  }

  // Transparent overlays composite onto the page the user locked from, so this path
  // must reach the renderer with that page intact. It skips the deepCleanPanel blank
  // below, which exists to stop a wallpaper ghosting over the old page — here the old
  // page IS the background.
  //
  // The "Entering sleep" popup still runs, because decoding an overlay takes a moment
  // and the user needs to see the press registered. It is drawn and then lifted back
  // out of the framebuffer, so it appears on the panel without ending up baked into
  // the background the overlay composites onto.
  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::TRANSPARENT_CUSTOM) {
    if (APP_STATE.lastSleepFromReader) {
      ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    }
    drawSleepPopupPreservingFrame(renderer);
    if (APP_STATE.lastSleepFromReader) {
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    }
    return renderTransparentCustomSleepScreen();
  }

  // The "Entering sleep" popup is a progress note for a lock that takes a moment.
  // A wallpaper lock does not need it: deepCleanPanel is about to blank the screen
  // anyway, so the popup is one extra differential paint the user sees for an
  // instant and then loses. The old fork skipped it on this path for the same
  // reason (its directWallpaperLock).
  const bool paintsWallpaper =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM ||
      (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM && !APP_STATE.lastSleepFromReader);
  if (!paintsWallpaper) {
    // Show popup with reader orientation only when going to sleep from reader
    if (APP_STATE.lastSleepFromReader) {
      ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
      GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    } else {
      GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
    }
  }

  deepCleanPanel();

  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::BLANK):
      return renderBlankSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM):
      return renderCustomSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER):
      return renderCoverSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      if (APP_STATE.lastSleepFromReader) {
        return renderCoverSleepScreen();
      } else {
        return renderCustomSleepScreen();
      }
    case (CrossPointSettings::SLEEP_SCREEN_MODE::STATS_DASHBOARD):
      return renderStatsDashboardSleepScreen();
    default:
      return renderDefaultSleepScreen();
  }
}

void SleepActivity::renderStatsDashboardSleepScreen() const {
  const std::string& path = APP_STATE.openEpubPath;
  if (path.empty() || !stats_dashboard::supportsBook(path)) {
    return renderDefaultSleepScreen();
  }

  RecentBook recent = recentBookForPath(path);
  stats_dashboard::DashboardData data;
  data.title = recent.title;
  data.progressPercent = static_cast<uint8_t>(std::clamp(recent.progressPercent, 0, 100));
  std::string cachePath;
  std::string coverPath;
  constexpr int kDashboardCoverHeight = 444;

  if (FsHelpers::hasEpubExtension(path)) {
    Epub epub(path, "/.crosspoint");
    cachePath = epub.getCachePath();
    const bool loaded = epub.load(false, true);
    if (data.title.empty() && loaded) data.title = epub.getTitle();
    coverPath = UITheme::getCoverThumbPath(recent.coverBmpPath, kDashboardCoverHeight);

    if (loaded) {
      HalFile progressFile;
      if (Storage.openFileForRead("DASH", cachePath + "/progress.bin", progressFile)) {
        uint8_t progress[6] = {};
        const int count = progressFile.read(progress, sizeof(progress));
        progressFile.close();
        if (count >= 4) {
          const int spineIndex = progress[0] | (progress[1] << 8);
          const int tocIndex = epub.getTocIndexForSpineIndex(spineIndex);
          if (tocIndex >= 0) data.chapter = epub.getTocItem(tocIndex).title;
        }
      }
    }
  } else if (FsHelpers::hasXtcExtension(path)) {
    Xtc xtc(path, "/.crosspoint");
    cachePath = xtc.getCachePath();
    const bool loaded = xtc.load();
    if (data.title.empty() && loaded) data.title = xtc.getTitle();
    coverPath = UITheme::getCoverThumbPath(recent.coverBmpPath, kDashboardCoverHeight);

    if (loaded) {
      HalFile progressFile;
      if (Storage.openFileForRead("DASH", cachePath + "/progress.bin", progressFile)) {
        uint8_t progress[4] = {};
        const int count = progressFile.read(progress, sizeof(progress));
        progressFile.close();
        if (count == 4) {
          const uint32_t page = progress[0] | (progress[1] << 8) | (progress[2] << 16) | (progress[3] << 24);
          data.progressPercent = xtc.calculateProgress(page);
          const auto& chapters = xtc.getChapters();
          const auto chapter = std::find_if(chapters.begin(), chapters.end(), [&](const xtc::ChapterInfo& item) {
            return page >= item.startPage && page <= item.endPage;
          });
          if (chapter != chapters.end()) data.chapter = chapter->name;
        }
      }
    }
  } else {
    Txt txt(path, "/.crosspoint");
    cachePath = txt.getCachePath();
    const bool loaded = txt.load();
    if (data.title.empty() && loaded) data.title = txt.getTitle();
    if (loaded && Storage.exists(txt.getCoverBmpPath().c_str())) coverPath = txt.getCoverBmpPath();
  }

  if (data.title.empty()) data.title = fileNameFromPath(path);

  const auto now = reading_stats::currentLocalDateTime();
  data.todayDay = now.valid ? now.dayIndex : 0;
  reading_stats::SdStatsFiles files;
  reading_stats::ReadingStatsStore statsStore(files);
  statsStore.load(cachePath + "/reading_stats.bin", data.book);
  statsStore.load(reading_stats::ReaderStatsSession::globalPath(), data.global);
  if (data.book.resetEpoch != data.global.resetEpoch) {
    data.book = {};
    data.book.resetEpoch = data.global.resetEpoch;
  }

  if (coverPath.empty() || !Storage.exists(coverPath.c_str())) {
    // Thumb generation is SD->SD image decode; the framebuffer is not needed until
    // the render below, so lend its 48KB to the decoder. The inflate window+state
    // then comes from the loaned block instead of the heap at the sleep-entry
    // low-water mark. The render path below starts with a full clearScreen, so the
    // scribbled buffer is fine. The loan MUST end before any render call.
    GfxRenderer::FrameBufferLoan loan(renderer);
    if (FsHelpers::hasEpubExtension(path)) {
      Epub epub(path, "/.crosspoint");
      if (epub.load(false, true) && epub.generateThumbBmp(kDashboardCoverHeight)) {
        coverPath = epub.getThumbBmpPath(kDashboardCoverHeight);
      }
    } else if (FsHelpers::hasXtcExtension(path)) {
      Xtc xtc(path, "/.crosspoint");
      if (xtc.load() && xtc.generateThumbBmp(kDashboardCoverHeight)) {
        coverPath = xtc.getThumbBmpPath(kDashboardCoverHeight);
      }
    } else {
      Txt txt(path, "/.crosspoint");
      if (txt.load() && txt.generateCoverBmp()) coverPath = txt.getCoverBmpPath();
    }
  }

  if (coverPath.empty() || !Storage.exists(coverPath.c_str())) {
    return renderDefaultSleepScreen();
  }
  data.imagePath = coverPath;
  if (!stats_dashboard::render(renderer, data)) renderDefaultSleepScreen();
}

void SleepActivity::renderCustomSleepScreen() const {
  // Check if we have a /.sleep (preferred) or /sleep directory
  const char* sleepDir = nullptr;
  auto dir = Storage.open("/.sleep");

  // Look for sleep.bmp on the root of the sd card to determine if we should
  // render a custom sleep screen instead of the default.
  // This takes priority over the /sleep folder.
  HalFile file;
  if (Storage.openFileForRead("SLP", "/sleep.bmp", file)) {
    const bool adaptiveTone =
        SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;
    Bitmap bitmap(file, true, adaptiveTone ? BitmapToneMapping::Adaptive : BitmapToneMapping::None);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Loading: /sleep.bmp");
      const SleepInfoOverlayScope overlayScope("/sleep.bmp");
      renderBitmapSleepScreen(bitmap);
      APP_STATE.lastSleepWallpaperPath = "/sleep.bmp";
      file.close();
      if (dir) dir.close();
      return;
    }
    file.close();
  }

  // /sleep.pxc (Lector Wallpaper Converter format), same root-priority tier as
  // /sleep.bmp. renderPxcSleepScreen opens the path itself and returns false when
  // the file is absent/invalid, so we just try it and fall through on failure.
  // Both devices render .pxc through the OEM 3-pass grayscale pipeline, matching the
  // BMP wallpaper and cover sleep screens. The X4 previously stalled here: its HALF
  // sequence powers the panel rails down, and the grayscale refresh that follows then
  // shared one activation with the rail ramp. Fixed at the driver-config level; see
  // src/platform/LectorSsd1677Config.cpp.
  // Quality is the user's call: Pretty is the 3-pass grayscale above, Fast is a single
  // 1-bit pass. Three panel refreshes instead of one is real time spent going to sleep,
  // which is worth trading away on a dithered image.
  const bool pxcGrayscale = SETTINGS.sleepImageQuality == CrossPointSettings::SLEEP_QUALITY_PRETTY;
  {
    const SleepInfoOverlayScope overlayScope("/sleep.pxc");
    if (renderPxcSleepScreen(renderer, "/sleep.pxc", pxcGrayscale, HalDisplay::HALF_REFRESH, &drawSleepInfoOverlay)) {
      LOG_INF("SLP", "Loaded: /sleep.pxc");
      APP_STATE.lastSleepWallpaperPath = "/sleep.pxc";
      if (dir) dir.close();
      return;
    }
  }

  if (dir && dir.isDirectory()) {
    sleepDir = "/.sleep";
  } else {
    dir = Storage.open("/sleep");
    if (dir && dir.isDirectory()) {
      sleepDir = "/sleep";
    }
  }

  if (sleepDir) {
    std::string chosen;

    // Rotation paused: keep showing the wallpaper that is already up instead of
    // picking a new one. Only honoured while that file is still sitting in this
    // folder — favouriting renames it, pausing moves it out, and deleting it from
    // the browser removes it, so a stale name here must fall through to a normal
    // pick rather than leaving the user with a blank sleep screen they cannot
    // explain.
    if (SETTINGS.wallpaperRotationPaused && !previousWallpaper.empty()) {
      const std::string prefix = std::string(sleepDir) + "/";
      if (previousWallpaper.rfind(prefix, 0) == 0 && Storage.exists(previousWallpaper.c_str())) {
        chosen = previousWallpaper.substr(prefix.size());
        LOG_INF("SLP", "rotation paused, holding %s", chosen.c_str());
      } else {
        LOG_INF("SLP", "rotation paused but held wallpaper is gone; picking a new one");
      }
    }

    const bool heldByPause = !chosen.empty();

    // The persistent line: fresh (newly indexed) wallpapers first, then the
    // shuffled lap — every wallpaper exactly once per lap, reshuffled at the
    // wrap. Cost per sleep is one index open plus a 160-byte read per inspected
    // slot; no folder scan. Falls through to the jump pick when the index is
    // absent, built for the other folder, or declared stale.
    bool pickedFromIndex = false;
    // Rotation line position of the picked wallpaper, for the optional
    // bottom-right badge. 0/0 = unknown (jump pick, pause hold), badge hidden.
    uint32_t linePosition = 0;
    uint32_t lineTotal = 0;
    if (chosen.empty() && !APP_STATE.sleepIndexNeedsRebuild) {
      namespace windex = crosspoint::sleep::windex;
      const uint32_t indexStartMs = millis();
      windex::Reader reader;
      if (reader.open() && reader.recordCount() > 0 && strcmp(windex::dirPathForId(reader.dirId()), sleepDir) == 0) {
        auto queueState = windex::loadQueueState();
        const std::string prefix = std::string(sleepDir) + "/";
        const auto nameAt = [&](const size_t i) { return reader.nameAt(i); };
        const auto liveInFolder = [&](const std::string& n) { return Storage.exists((prefix + n).c_str()); };
        const auto counterpart = [](const std::string& n) { return FavoriteImage::favoriteCounterpart(n); };
        auto result = sleep_queue::pickNext(queueState, reader.recordCount(), esp_random(), esp_random(), nameAt,
                                            liveInFolder, counterpart);
        // Within a lap repeats are impossible by construction; only a reseed
        // boundary can land on the wallpaper already holding the panel.
        if (!heldByPause && !result.basename.empty() && reader.recordCount() > 1 &&
            prefix + result.basename == previousWallpaper) {
          auto again = sleep_queue::pickNext(queueState, reader.recordCount(), esp_random(), esp_random(), nameAt,
                                             liveInFolder, counterpart);
          const bool wrapped = result.lapWrapped || again.lapWrapped;
          if (!again.basename.empty()) result = std::move(again);
          result.lapWrapped = wrapped;  // a wrap in either pick still ended the lap
        }
        // Lap over: every wallpaper has now been shown once, so this is the
        // cheapest possible moment to compact the holes that in-place deletes
        // left behind. Flags the next cold boot; nothing happens tonight.
        if (result.lapWrapped) windex::noteLapWrapped();
        // Persist the advanced state even when the render below fails: the
        // cursor must step PAST a present-but-unrenderable file, or every
        // sleep would retry it and show the logo face forever. A crash before
        // the render costs one skipped wallpaper, nothing more.
        windex::storeQueueState(queueState);
        stateDirty = true;
        if (result.needsRebuild) {
          // Too many dead slots this pick: use the jump pick tonight and let
          // the next cold boot rebuild the index.
          APP_STATE.sleepIndexNeedsRebuild = true;
          LOG_INF("SLP", "index stale, falling back to jump pick");
        } else if (!result.basename.empty()) {
          chosen = std::move(result.basename);
          pickedFromIndex = true;
          // Served-this-loop count, from the post-advance state: cursor.position
          // lap picks plus the drained part of the fresh region (fresh records
          // start at seededCount). 0 only right after a wrap, which means the
          // pick that COMPLETED the loop — show it as total/total, not 0.
          lineTotal = static_cast<uint32_t>(reader.recordCount());
          const uint32_t served = queueState.cursor.position + (queueState.freshNext - queueState.cursor.seededCount);
          linePosition = served == 0 || served > lineTotal ? lineTotal : served;
          LOG_INF("SLP", "index pick in %ums", static_cast<unsigned>(millis() - indexStartMs));
        }
      }
    }

    // Fast path: seek straight to a random directory slot (see pickWallpaperByJump).
    const uint32_t pickStartMs = millis();
    if (chosen.empty()) chosen = pickWallpaperByJump(dir);
    if (!chosen.empty() && !heldByPause && !pickedFromIndex) {
      LOG_INF("SLP", "jump pick in %ums", static_cast<unsigned>(millis() - pickStartMs));
    }

    // Never show the same wallpaper twice in a row. Each lock picks independently, so
    // the pick can legitimately land on the file already on the panel; a couple of
    // re-rolls make a visible repeat unlikely without pretending the folder holds more
    // than it does. A one-wallpaper folder simply keeps showing it, which is correct,
    // and a paused rotation is meant to repeat, so it is left alone. An index pick
    // handled its own reseed-boundary repeat above and must not be re-rolled here —
    // a jump re-roll would break the line's no-repeat guarantee.
    if (!heldByPause && !pickedFromIndex && !chosen.empty() && !previousWallpaper.empty()) {
      const std::string prefix = std::string(sleepDir) + "/";
      for (int retry = 0; retry < 2 && prefix + chosen == previousWallpaper; retry++) {
        const std::string again = pickWallpaperByJump(dir);
        if (again.empty()) break;  // jump gave up; keep what we have
        chosen = again;
      }
    }

    // Fallback: walk the whole folder with reservoir sampling — keep the k-th valid
    // file with probability 1/k, so every file is equally likely, using O(1) memory
    // and no per-file header read. Only reached when the jump cannot resolve a name.
    uint32_t seen = 0;
    uint32_t scanned = 0;
    const uint32_t scanStartMs = millis();
    char name[256];  // FAT long-file-name maximum (255 chars + terminator)
    dir.rewindDirectory();
    for (auto dirFile = chosen.empty() ? dir.openNextFile() : HalFile(); dirFile; dirFile = dir.openNextFile()) {
      // A wallpaper folder can hold thousands of files, and this walk runs inline on the
      // loop task while going to sleep. Without a periodic yield the task never returns
      // to the scheduler, the task watchdog has no chance to be fed, and sleep entry can
      // wedge hard enough to need a physical reset.
      if ((++scanned & 0x3F) == 0) {
        resetTaskWatchdogIfSubscribed();
        vTaskDelay(1);
      }
      const bool isDir = dirFile.isDirectory();
      dirFile.getName(name, sizeof(name));
      dirFile.close();  // only the name is needed; never open/parse the file here
      if (isDir || !isWallpaperName(name)) continue;
      ++seen;
      if (random(static_cast<long>(seen)) == 0) chosen = name;
    }
    if (scanned > 0) {
      LOG_INF("SLP", "fallback scan: %u entries (%u wallpapers) in %ums", scanned, seen,
              static_cast<unsigned>(millis() - scanStartMs));
    }
    if (!chosen.empty()) {
      const auto filename = std::string(sleepDir) + "/" + chosen;
      LOG_INF("SLP", "Randomly loading: %s", filename.c_str());
      delay(100);
      const SleepInfoOverlayScope overlayScope(filename, linePosition, lineTotal);
      if (hasPxcExtension(chosen)) {
        if (renderPxcSleepScreen(renderer, filename, pxcGrayscale, HalDisplay::HALF_REFRESH, &drawSleepInfoOverlay)) {
          APP_STATE.lastSleepWallpaperPath = filename;
          dir.close();
          return;
        }
      } else {
        HalFile randFile;
        if (Storage.openFileForRead("SLP", filename, randFile)) {
          // Same rule the /sleep.bmp path above uses: stretch the tone range only when
          // the user has asked for no cover filter, so a filtered image still looks the
          // way they set it. Applies to .bmp wallpapers only; .pxc took the branch above
          // and is already quantised to four levels when the file is written.
          const bool adaptiveTone =
              SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;
          Bitmap bitmap(randFile, true, adaptiveTone ? BitmapToneMapping::Adaptive : BitmapToneMapping::None);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            renderBitmapSleepScreen(bitmap);
            APP_STATE.lastSleepWallpaperPath = filename;
            randFile.close();
            dir.close();
            return;
          }
          randFile.close();
        }
      }
    }
  }
  if (dir) dir.close();

  renderDefaultSleepScreen();
}

// One blank FULL pass before painting a sleep face, exactly as the pre-rebase fork did
// it (its RefreshIntent::DeepClean was Buffer + FULL over a white screen). Lock can
// happen over any screen, and every sleep render below uses the calibrated differential
// HALF/graybase waveforms, which would otherwise leave the prior content ghosting
// through the wallpaper for the whole sleep.
//
// This deliberately does NOT try to be a stronger wipe. Escalating it here (black-then-
// white, then three cycles of that) was tried and did not stop the ghost, because the
// ghost was never a wipe problem: the pre-rebase fork ghosted nothing with this single
// pass. What it also had, and what the rebase had dropped, is the anti-ghosting cap in
// HalDisplay that stops a long run of FAST passes from trapping charge in the first
// place. That cap is back; this stays as it was.
void SleepActivity::deepCleanPanel() const {
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
}

// Sleep screens paint with a single HALF refresh (stock parity): the OEM X4
// firmware's only clean refresh in normal operation is the single-pass 0xD7
// sequence, used once for the sleep image. It never runs the multi-flash GC
// waveform (0xF7) that FULL_REFRESH selects (#2471's blinking complaint).
void SleepActivity::renderDefaultSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_CROSSPOINT), true, EpdFontFamily::REGULAR);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_SLEEPING));

  // Make sleep screen dark unless light is selected in settings
  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap, const bool preserveBackground) const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto placement = calculateBitmapPlacement(bitmap.getWidth(), bitmap.getHeight(), renderer);
  const int x = placement.x;
  const int y = placement.y;
  const float cropX = placement.cropX;
  const float cropY = placement.cropY;

  LOG_DBG("SLP", "bitmap %d x %d, screen %d x %d", bitmap.getWidth(), bitmap.getHeight(), pageWidth, pageHeight);
  LOG_DBG("SLP", "drawing to %d x %d", x, y);
  if (!preserveBackground) renderer.clearScreen();

  // The cover filter describes how a full-screen wallpaper should look. An overlay is
  // composited onto the retained page, so the filter does not apply: NO_FILTER's tone
  // rule would gate grayscale off for no reason, and INVERTED would flip the page under
  // the art as well. Preserved backgrounds therefore always take the grayscale path.
  const bool hasGreyscale =
      bitmap.hasGreyscale() && (preserveBackground || SETTINGS.sleepScreenCoverFilter ==
                                                          CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER);

  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);

  if (!preserveBackground &&
      SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  // Drawn after the filter so the label is never inverted along with the image,
  // and once per pass below for the same reason the bitmap is. No-ops unless a
  // SleepInfoOverlayScope named a wallpaper, so the cover face draws nothing.
  drawSleepInfoOverlay(renderer);

  if (hasGreyscale) {
    // OEM grayscale pipeline base. Must stay HALF: the gray nudge LUT is
    // calibrated against the pixel state the single-pass HALF waveform leaves
    // behind. A FULL (GC) base parks pixels in a different charge state and
    // the differential nudge then lands unevenly (blotchy noise in gray areas).
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }

  if (hasGreyscale) {
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    drawSleepInfoOverlay(renderer);
    renderer.copyGrayscaleLsbBuffers();

    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    drawSleepInfoOverlay(renderer);
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }
}

bool SleepActivity::renderSleepOverlayFile(HalFile& file, const char* pathForLog) const {
  const auto alphaResult = tryRenderTransparentOverlayBmp(file, renderer, pathForLog);
  if (alphaResult == AlphaOverlayResult::Rendered) return true;
  if (alphaResult == AlphaOverlayResult::Error) return false;

  // Not a usable alpha image (8/24-bit, or 32-bit but fully opaque). Draw it as an
  // ordinary bitmap over the retained page instead of rejecting it.
  Bitmap bitmap(file, true);
  const auto parseResult = bitmap.parseHeaders();
  if (parseResult != BmpReaderError::Ok) {
    LOG_ERR("SLP", "Invalid sleep overlay BMP %s: %s", pathForLog, Bitmap::errorToString(parseResult));
    return false;
  }

  LOG_DBG("SLP", "Rendering regular BMP sleep overlay: %s (%dx%d)", pathForLog, bitmap.getWidth(), bitmap.getHeight());
  // drawBitmap leaves white pixels untouched, so skipping the initial clear makes
  // white act as transparent while keeping the existing grayscale pipeline.
  renderBitmapSleepScreen(bitmap, true);
  return true;
}

bool SleepActivity::renderTransparentOverlayPng(const std::string& path) const {
  ImageDimensions dimensions;
  if (!PngToFramebufferConverter::getDimensionsStatic(path, dimensions)) return false;

  const auto placement = calculateBitmapPlacement(dimensions.width, dimensions.height, renderer);
  RenderConfig config;
  config.x = placement.x;
  config.y = placement.y;
  config.maxWidth = renderer.getScreenWidth();
  config.maxHeight = renderer.getScreenHeight();
  config.useDithering = false;
  config.sourceCropX = placement.cropX;
  config.sourceCropY = placement.cropY;
  config.useExactDimensions = placement.cropX > 0.0f || placement.cropY > 0.0f;
  config.preserveAlpha = true;

  PngToFramebufferConverter converter;
  LOG_DBG("SLP", "Rendering transparent PNG overlay: %s (%dx%d)", path.c_str(), dimensions.width, dimensions.height);

  if (!converter.decodeToFramebuffer(path, renderer, config)) return false;
  renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  if (!converter.decodeToFramebuffer(path, renderer, config)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return true;
  }
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  if (!converter.decodeToFramebuffer(path, renderer, config)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return true;
  }
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  return true;
}

bool SleepActivity::renderSleepOverlayPath(const std::string& path) const {
  if (FsHelpers::hasPngExtension(path)) {
    return Storage.exists(path.c_str()) && renderTransparentOverlayPng(path);
  }

  HalFile file;
  return Storage.openFileForRead("SLP", path, file) && renderSleepOverlayFile(file, path.c_str());
}

void SleepActivity::renderTransparentCustomSleepScreen() const {
  if (renderSleepOverlayPath(TRANSPARENT_SLEEP_ROOT_BMP)) return;
  if (renderSleepOverlayPath(TRANSPARENT_SLEEP_ROOT_PNG)) return;

  std::string selectedPath = pickOverlayFromDir(TRANSPARENT_SLEEP_DIR);
  if (selectedPath.empty()) selectedPath = pickOverlayFromDir(TRANSPARENT_SLEEP_LEGACY_DIR);

  if (!selectedPath.empty() && renderSleepOverlayPath(selectedPath)) return;

  // Nothing to composite. The panel still holds the page the user locked from, which
  // reads as "sleep did nothing", so fall back to a real sleep face — and blank first,
  // since this path skipped deepCleanPanel on the way in.
  LOG_ERR("SLP", "No valid transparent sleep overlay found");
  deepCleanPanel();
  renderDefaultSleepScreen();
}

void SleepActivity::renderCoverSleepScreen() const {
  void (SleepActivity::*renderNoCoverSleepScreen)() const;
  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      renderNoCoverSleepScreen = &SleepActivity::renderCustomSleepScreen;
      break;
    default:
      renderNoCoverSleepScreen = &SleepActivity::renderDefaultSleepScreen;
      break;
  }

  if (APP_STATE.openEpubPath.empty()) {
    return (this->*renderNoCoverSleepScreen)();
  }

  std::string coverBmpPath;
  bool cropped = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;

  // Check if the current book is XTC, TXT, or EPUB
  if (FsHelpers::hasXtcExtension(APP_STATE.openEpubPath)) {
    // Handle XTC file
    Xtc lastXtc(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastXtc.load()) {
      LOG_ERR("SLP", "Failed to load last XTC");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastXtc.generateCoverBmp()) {
      LOG_ERR("SLP", "Failed to generate XTC cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastXtc.getCoverBmpPath();
  } else if (FsHelpers::hasTxtExtension(APP_STATE.openEpubPath)) {
    // Handle TXT file - looks for cover image in the same folder
    Txt lastTxt(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastTxt.load()) {
      LOG_ERR("SLP", "Failed to load last TXT");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastTxt.generateCoverBmp()) {
      LOG_ERR("SLP", "No cover image found for TXT file");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastTxt.getCoverBmpPath();
  } else if (FsHelpers::hasEpubExtension(APP_STATE.openEpubPath)) {
    // Handle EPUB file
    Epub lastEpub(APP_STATE.openEpubPath, "/.crosspoint");
    // Skip loading css since we only need metadata here
    if (!lastEpub.load(true, true)) {
      LOG_ERR("SLP", "Failed to load last epub");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastEpub.generateCoverBmp(cropped)) {
      LOG_ERR("SLP", "Failed to generate cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastEpub.getCoverBmpPath(cropped);
  } else {
    return (this->*renderNoCoverSleepScreen)();
  }

  HalFile file;
  if (Storage.openFileForRead("SLP", coverBmpPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Rendering sleep cover: %s", coverBmpPath.c_str());
      renderBitmapSleepScreen(bitmap);
      return;
    }
  }

  return (this->*renderNoCoverSleepScreen)();
}

void SleepActivity::renderLastScreenSleepScreen() const {
  const auto pageHeight = renderer.getScreenHeight();
  renderer.drawImage(MoonIcon, 0, pageHeight - MOONICON_HEIGHT, MOONICON_WIDTH, MOONICON_HEIGHT);
  if (gpio.deviceIsX3()) {
    // The controller still holds the displayed page, so its differential base
    // waveform can add the moon without a full-screen flash.
    renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
}

void SleepActivity::renderBlankSleepScreen() const {
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
