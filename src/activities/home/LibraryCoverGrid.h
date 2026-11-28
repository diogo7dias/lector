#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "activities/home/LibraryGridGeometry.h"

class GfxRenderer;

namespace library_grid {

struct GridBook {
  std::string title;
  std::string author;
  std::string path;
  uint8_t percent = 255;  // 255 = unread, 0..99 = reading, 100 = read
};

// Renders one page of book covers in LibraryActivity's Grid mode.
// On C3, each tile streams directly from thumb.bmp one at a time (bounded RAM).
// On X4 Pro (BOARD_HAS_PSRAM), caches the visible page's 2-bit thumbnails in PSRAM
// so cursor moves repaint without re-reading SD.
class CoverGridRenderer {
 public:
  void invalidateCache();

  void drawPage(GfxRenderer& renderer, const PageLayout& layout, const GridBook* books, int count, int selectedInPage,
                int pageKey, uint32_t generation);

 private:
  void drawTypographicCard(GfxRenderer& renderer, const Box& cover, const GridBook& book) const;
  void drawOverlays(GfxRenderer& renderer, const TileLayout& tile, uint8_t percent, bool selected) const;

#if defined(BOARD_HAS_PSRAM)
  struct CachedThumb {
    bool checked = false;
    bool hasThumb = false;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t rowBytes = 0;
    std::unique_ptr<uint8_t[]> pixels;  // Packed 2-bit top-down rows (PSRAM via >4 KB malloc)
  };
  int cachedPageKey = -1;
  uint32_t cachedGeneration = 0;
  CachedThumb cache[kMaxPageTiles];

  bool drawCachedThumb(GfxRenderer& renderer, int slot, const Box& cover, const std::string& thumbPath);
  bool loadThumbIntoSlot(CachedThumb& slot, const std::string& thumbPath);
#endif
};

}  // namespace library_grid
