#include "LibraryCoverGrid.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <ThumbResampler.h>

#include <algorithm>
#include <cstring>

#include "fontIds.h"
#include "util/BookCacheUtils.h"

namespace library_grid {
namespace {

bool drawThumbStreaming(GfxRenderer& renderer, const Box& cover, const std::string& thumbPath) {
  if (thumbPath.empty() || !Storage.exists(thumbPath.c_str())) return false;
  HalFile file;
  if (!Storage.openFileForRead("GRID", thumbPath, file)) return false;
  Bitmap bitmap(file, /*dithering=*/false);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) return false;
  renderer.drawBitmap(bitmap, cover.x, cover.y, cover.width, cover.height);
  return true;
}

}  // namespace

void CoverGridRenderer::invalidateCache() {
#if defined(BOARD_HAS_PSRAM)
  cachedPageKey = -1;
  cachedGeneration = 0;
  for (auto& slot : cache) {
    slot.checked = false;
    slot.hasThumb = false;
    slot.pixels.reset();
  }
#endif
}

#if defined(BOARD_HAS_PSRAM)
bool CoverGridRenderer::loadThumbIntoSlot(CachedThumb& slot, const std::string& thumbPath) {
  slot.checked = true;
  slot.hasThumb = false;
  slot.pixels.reset();
  if (thumbPath.empty() || !Storage.exists(thumbPath.c_str())) return false;

  HalFile file;
  if (!Storage.openFileForRead("GRID", thumbPath, file)) return false;
  uint8_t hdr[54];
  if (file.read(hdr, sizeof(hdr)) != static_cast<int>(sizeof(hdr)) || hdr[0] != 'B' || hdr[1] != 'M') return false;
  const uint32_t offset = thumb_resampler::loadLE32(hdr + 10);
  const int32_t rawW = static_cast<int32_t>(thumb_resampler::loadLE32(hdr + 18));
  const int32_t rawH = static_cast<int32_t>(thumb_resampler::loadLE32(hdr + 22));
  const uint16_t bpp = thumb_resampler::loadLE16(hdr + 28);
  const bool topDown = rawH < 0;
  const int w = rawW;
  const int h = topDown ? -rawH : rawH;
  if (bpp != 2 || w <= 0 || h <= 0 || w > 256 || h > 384) return false;

  const int rowBytes = (w * 2 + 31) / 32 * 4;
  const size_t totalBytes = static_cast<size_t>(rowBytes) * static_cast<size_t>(h);
  // Ensure allocation > 4096 B lands in PSRAM (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096).
  auto buf = makeUniqueNoThrow<uint8_t[]>(std::max<size_t>(totalBytes, 4097));
  if (!buf) return false;

  for (int y = 0; y < h; ++y) {
    const int fileRow = topDown ? y : (h - 1 - y);
    if (!file.seekSet(offset + static_cast<uint32_t>(fileRow) * static_cast<uint32_t>(rowBytes))) return false;
    if (file.read(buf.get() + static_cast<size_t>(y) * static_cast<size_t>(rowBytes), rowBytes) != rowBytes) {
      return false;
    }
  }
  slot.width = static_cast<uint16_t>(w);
  slot.height = static_cast<uint16_t>(h);
  slot.rowBytes = static_cast<uint16_t>(rowBytes);
  slot.pixels = std::move(buf);
  slot.hasThumb = true;
  return true;
}

bool CoverGridRenderer::drawCachedThumb(GfxRenderer& renderer, const int slotIdx, const Box& cover,
                                        const std::string& thumbPath) {
  CachedThumb& slot = cache[slotIdx];
  if (!slot.checked) loadThumbIntoSlot(slot, thumbPath);
  if (!slot.hasThumb || !slot.pixels) return false;

  // Center and scale nearest-neighbor from cached 2-bit pixels (threshold < 2 -> black ink).
  const int drawW = std::min<int>(cover.width, slot.width);
  const int drawH = std::min<int>(cover.height, slot.height);
  const int offX = cover.x + (cover.width - drawW) / 2;
  const int offY = cover.y + (cover.height - drawH) / 2;
  for (int dy = 0; dy < drawH; ++dy) {
    const int sy = (dy * slot.height) / drawH;
    const uint8_t* row = slot.pixels.get() + static_cast<size_t>(sy) * slot.rowBytes;
    for (int dx = 0; dx < drawW; ++dx) {
      const int sx = (dx * slot.width) / drawW;
      const uint8_t level = (row[sx >> 2] >> (6 - ((sx & 3) << 1))) & 0x03;
      if (level < 2) renderer.drawPixel(offX + dx, offY + dy, true);
    }
  }
  return true;
}
#endif

void CoverGridRenderer::drawTypographicCard(GfxRenderer& renderer, const Box& cover, const GridBook& book) const {
  renderer.fillRect(cover.x, cover.y, cover.width, cover.height, false);
  renderer.drawRect(cover.x, cover.y, cover.width, cover.height, true);
  if (cover.width > 12 && cover.height > 12) {
    renderer.drawRect(cover.x + 3, cover.y + 3, cover.width - 6, cover.height - 6, true);
  }

  const int pad = 8;
  const int textW = std::max(16, cover.width - 2 * pad);
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  int y = cover.y + pad + 4;

  const auto titleLines = renderer.wrappedText(UI_10_FONT_ID, book.title.c_str(), textW, 3, EpdFontFamily::BOLD);
  for (const auto& line : titleLines) {
    if (y + lineH > cover.y + cover.height - pad) break;
    renderer.drawText(UI_10_FONT_ID, cover.x + pad, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += lineH;
  }

  if (!book.author.empty() && y + lineH + 6 <= cover.y + cover.height - pad) {
    y += 4;
    renderer.fillRect(cover.x + pad, y, std::min(textW, 36), 1, true);
    y += 6;
    const auto authorLines = renderer.wrappedText(UI_10_FONT_ID, book.author.c_str(), textW, 2);
    for (const auto& line : authorLines) {
      if (y + lineH > cover.y + cover.height - pad) break;
      renderer.drawText(UI_10_FONT_ID, cover.x + pad, y, line.c_str(), true);
      y += lineH;
    }
  }
}

void CoverGridRenderer::drawOverlays(GfxRenderer& renderer, const TileLayout& tile, const uint8_t percent,
                                     const bool selected) const {
  // Border around the cover art.
  renderer.drawRect(tile.cover.x, tile.cover.y, tile.cover.width, tile.cover.height, true);

  // Read ribbon in the top-right corner when finished.
  if (percent == 100) {
    const char* label = tr(STR_READ_BADGE);
    const int textW = renderer.getTextWidth(SMALL_FONT_ID, label);
    const int textH = renderer.getLineHeight(SMALL_FONT_ID);
    const int pillW = std::min(tile.cover.width - 4, textW + 8);
    const int pillH = textH + 2;
    const int pillX = tile.cover.x + tile.cover.width - pillW - 2;
    const int pillY = tile.cover.y + 2;
    renderer.fillRect(pillX - 1, pillY - 1, pillW + 2, pillH + 2, false);
    renderer.fillRect(pillX, pillY, pillW, pillH, true);
    renderer.drawText(SMALL_FONT_ID, pillX + 4, pillY + 1, label, false);
  }

  // Thin progress bar directly beneath the cover for started/read books.
  if (percent <= 100) {
    renderer.drawRect(tile.progressBar.x, tile.progressBar.y, tile.progressBar.width, tile.progressBar.height, true);
    const int fillW = (tile.progressBar.width * percent) / 100;
    if (fillW > 0) {
      renderer.fillRect(tile.progressBar.x, tile.progressBar.y, fillW, tile.progressBar.height, true);
    }
  }

  // Crisp focus frame around the selected tile.
  if (selected) {
    renderer.drawRect(tile.cell.x, tile.cell.y, tile.cell.width, tile.cell.height, 3, true);
  }
}

void CoverGridRenderer::drawPage(GfxRenderer& renderer, const PageLayout& layout, const GridBook* books,
                                 const int count, const int selectedInPage, const int pageKey,
                                 const uint32_t generation) {
#if defined(BOARD_HAS_PSRAM)
  if (cachedPageKey != pageKey || cachedGeneration != generation) {
    invalidateCache();
    cachedPageKey = pageKey;
    cachedGeneration = generation;
  }
#else
  (void)pageKey;
  (void)generation;
#endif

  const int limit = std::min(count, layout.perPage);
  for (int i = 0; i < limit; ++i) {
    const TileLayout& tile = layout.tiles[i];
    const GridBook& book = books[i];
    const std::string cacheDir = bookCacheDirForPath(book.path);
    const std::string thumbPath = cacheDir.empty() ? std::string() : cacheDir + "/thumb.bmp";

    bool drewThumb = false;
#if defined(BOARD_HAS_PSRAM)
    drewThumb = drawCachedThumb(renderer, i, tile.cover, thumbPath);
#else
    drewThumb = drawThumbStreaming(renderer, tile.cover, thumbPath);
#endif
    if (!drewThumb) {
      drawTypographicCard(renderer, tile.cover, book);
    }
    drawOverlays(renderer, tile, book.percent, i == selectedInPage);
  }
}

}  // namespace library_grid
