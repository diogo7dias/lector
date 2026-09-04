#include "SleepInfoOverlay.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "CrossPointSettings.h"
#include "fontIds.h"
#include "util/FavoriteImage.h"

namespace {

// The wallpaper currently being rendered, owned by SleepInfoOverlayScope. Empty
// means "no wallpaper in scope", which disables the overlay entirely.
std::string g_sourcePath;
// Rotation line position of that wallpaper ("N of M this loop"); 0/0 = unknown
// (fixed sleep file, jump-pick fallback), which hides the position badge.
uint32_t g_position = 0;
uint32_t g_total = 0;

enum class Corner { BottomLeft, BottomRight };

// Filled box + text in a bottom safe corner. White text on a black box so it
// reads over any wallpaper. Both badges share this so the filename, the "F"
// mark and the rotation position are the same shape on screen.
void drawLabel(const GfxRenderer& renderer, const std::string& text, const Corner corner = Corner::BottomLeft) {
  if (text.empty()) return;
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  constexpr int safeInset = 18;
  constexpr int paddingX = 4;
  constexpr int paddingY = 2;
  const int textLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int maxBoxWidth = std::max(1, screenWidth - safeInset * 2);
  const int maxTextWidth = std::max(1, maxBoxWidth - paddingX * 2 - 2);

  const std::vector<std::string> lines = renderer.wrappedText(UI_10_FONT_ID, text.c_str(), maxTextWidth, 2);
  if (lines.empty()) return;

  int textWidth = 0;
  for (const auto& line : lines) {
    textWidth = std::max(textWidth, renderer.getTextWidth(UI_10_FONT_ID, line.c_str(), EpdFontFamily::REGULAR));
  }
  const int boxWidth = std::min(textWidth + paddingX * 2, maxBoxWidth);
  const int boxHeight = static_cast<int>(lines.size()) * textLineHeight + paddingY * 2;
  const int boxX = corner == Corner::BottomRight ? std::max(safeInset, screenWidth - safeInset - boxWidth) : safeInset;
  const int boxY = std::max(safeInset, screenHeight - boxHeight - safeInset);

  // The black box must be drawn in EVERY pass: in the grayscale plane passes
  // fillRect(true) clears the plane bits over the box area, which is what stops
  // the wallpaper's gray nudges from bleeding through it. The white text and
  // border must be drawn ONLY in the BW base pass: the 1-bit glyph/rect path
  // ignores the render mode and would set the LSB+MSB plane bits (the dark-grey
  // nudge cell), turning the white pixels dark grey on 3-pass wallpapers.
  renderer.fillRect(boxX, boxY, boxWidth, boxHeight, true);
  if (renderer.getRenderMode() == GfxRenderer::BW) {
    renderer.drawRect(boxX, boxY, boxWidth, boxHeight, 1, false);
    for (size_t i = 0; i < lines.size(); ++i) {
      renderer.drawText(UI_10_FONT_ID, boxX + paddingX, boxY + paddingY + static_cast<int>(i) * textLineHeight,
                        lines[i].c_str(), false, EpdFontFamily::REGULAR);
    }
  }
}

}  // namespace

SleepInfoOverlayScope::SleepInfoOverlayScope(const std::string& sourcePath, const uint32_t position,
                                             const uint32_t total) {
  g_sourcePath = sourcePath;
  g_position = position;
  g_total = total;
}

SleepInfoOverlayScope::~SleepInfoOverlayScope() {
  g_sourcePath.clear();
  g_position = 0;
  g_total = 0;
}

void drawSleepInfoOverlay(GfxRenderer& renderer) {
  if (g_sourcePath.empty()) return;

  if (SETTINGS.showSleepImageFilename) {
    drawLabel(renderer, FavoriteImage::displayNameForPath(g_sourcePath));
  } else if (SETTINGS.showSleepFavoriteBadge && FavoriteImage::isFavoritePath(g_sourcePath)) {
    // Just "F" — the box border reads as the brackets.
    drawLabel(renderer, "F");
  }
  if (SETTINGS.showSleepWallpaperPosition && g_total > 0 && g_position > 0) {
    // Same boxed shape as the "F" mark, mirrored to the other corner.
    char text[24];
    snprintf(text, sizeof(text), "%lu / %lu", static_cast<unsigned long>(g_position),
             static_cast<unsigned long>(g_total));
    drawLabel(renderer, text, Corner::BottomRight);
  }
}
