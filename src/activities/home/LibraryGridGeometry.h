#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

// Pure layout and navigation arithmetic for LibraryActivity's Cover Grid view.
// 3x3 (9 tiles/page) on X4 Pro, 2x3 (6 tiles/page) on X3/X4 (C3).
namespace library_grid {

constexpr int kMaxPageTiles = 9;
constexpr int kThumbAspectW = 132;
constexpr int kThumbAspectH = 196;
constexpr int kProgressBarHeight = 4;
constexpr int kGap = 12;

struct Box {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  bool contains(const int px, const int py) const { return px >= x && px < x + width && py >= y && py < y + height; }
};

struct TileLayout {
  Box cell{};
  Box cover{};
  Box progressBar{};
};

struct PageLayout {
  int cols = 2;
  int rows = 3;
  int perPage = 6;
  std::array<TileLayout, kMaxPageTiles> tiles{};
};

inline constexpr int defaultCols(const bool highRamBoard, const bool landscape) {
  if (highRamBoard) return 3;
  return landscape ? 3 : 2;
}

inline constexpr int defaultRows(const bool highRamBoard, const bool landscape) {
  if (highRamBoard) return 3;
  return landscape ? 2 : 3;
}

inline PageLayout computeLayout(const Box& body, const bool highRamBoard) {
  PageLayout out;
  const bool landscape = body.width > body.height;
  out.cols = defaultCols(highRamBoard, landscape);
  out.rows = defaultRows(highRamBoard, landscape);
  out.perPage = std::min(kMaxPageTiles, out.cols * out.rows);

  const int cellW = std::max(40, (body.width - (out.cols - 1) * kGap) / out.cols);
  const int cellH = std::max(56, (body.height - (out.rows - 1) * kGap) / out.rows);
  const int totalW = out.cols * cellW + (out.cols - 1) * kGap;
  const int totalH = out.rows * cellH + (out.rows - 1) * kGap;
  const int startX = body.x + std::max(0, (body.width - totalW) / 2);
  const int startY = body.y + std::max(0, (body.height - totalH) / 2);

  for (int i = 0; i < out.perPage; ++i) {
    const int r = i / out.cols;
    const int c = i % out.cols;
    TileLayout& tile = out.tiles[i];
    tile.cell = {startX + c * (cellW + kGap), startY + r * (cellH + kGap), cellW, cellH};

    // Fit 132:196 cover inside cell leaving 4 px below for the progress bar + 2 px gap.
    const int availW = std::max(24, cellW - 4);
    const int availH = std::max(32, cellH - kProgressBarHeight - 6);
    int coverW = availW;
    int coverH = (coverW * kThumbAspectH) / kThumbAspectW;
    if (coverH > availH) {
      coverH = availH;
      coverW = std::max(20, (coverH * kThumbAspectW) / kThumbAspectH);
    }
    const int coverX = tile.cell.x + (cellW - coverW) / 2;
    const int coverY = tile.cell.y + (availH - coverH) / 2 + 2;
    tile.cover = {coverX, coverY, coverW, coverH};
    tile.progressBar = {coverX, coverY + coverH + 2, coverW, kProgressBarHeight};
  }
  return out;
}

inline int pageStartFor(const int selected, const int perPage) {
  if (selected <= 0 || perPage <= 0) return 0;
  return (selected / perPage) * perPage;
}

inline int tileAt(const PageLayout& layout, const int visibleCount, const int px, const int py) {
  const int limit = std::min(visibleCount, layout.perPage);
  for (int i = 0; i < limit; ++i) {
    if (layout.tiles[i].cell.contains(px, py)) return i;
  }
  return -1;
}

inline int moveCursor(const int current, const int total, const int delta) {
  if (total <= 0) return 0;
  int next = current + delta;
  if (next < 0) next = total - 1;
  if (next >= total) next = 0;
  return next;
}

inline int movePage(const int current, const int total, const int perPage, const int direction) {
  if (total <= 0 || perPage <= 0) return 0;
  const int page = current / perPage;
  const int pages = (total + perPage - 1) / perPage;
  if (pages <= 1) return current;
  const int nextPage = (page + direction + pages) % pages;
  const int inPage = current % perPage;
  return std::min(total - 1, nextPage * perPage + inPage);
}

}  // namespace library_grid
