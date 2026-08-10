#include "SleepInfoOverlay.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>

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

// Filled box + text in the bottom-left safe corner. White text on a black box so
// it reads over any wallpaper.
void drawLabel(const GfxRenderer& renderer, const std::string& text) {
  if (text.empty()) return;
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  constexpr int safeInset = 18;
  constexpr int paddingX = 4;
  constexpr int paddingY = 2;
  const int textLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int maxBoxWidth = std::max(1, screenWidth - safeInset * 2);
  const int maxTextWidth = std::max(1, maxBoxWidth - paddingX * 2 - 2);

  const std::string shown = renderer.truncatedText(UI_10_FONT_ID, text.c_str(), maxTextWidth);
  const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, shown.c_str(), EpdFontFamily::REGULAR);
  const int boxWidth = std::min(textWidth + paddingX * 2, maxBoxWidth);
  const int boxHeight = textLineHeight + paddingY * 2;
  const int boxX = safeInset;
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
    renderer.drawText(UI_10_FONT_ID, boxX + paddingX, boxY + paddingY, shown.c_str(), false, EpdFontFamily::REGULAR);
  }
}

// "position / total" plus a progress bar in the bottom-right safe corner.
// Same box style and — critically — the same render-pass rules as drawLabel:
// the black box fills in every pass, white pixels only in the BW base pass.
void drawPositionBadge(const GfxRenderer& renderer, const uint32_t position, const uint32_t total) {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  constexpr int safeInset = 18;
  constexpr int paddingX = 4;
  constexpr int paddingY = 2;
  constexpr int barHeight = 4;
  constexpr int barGap = 3;
  // A floor so the bar stays readable as a bar when the numbers are short.
  constexpr int minInnerWidth = 56;

  char text[24];
  snprintf(text, sizeof(text), "%lu / %lu", static_cast<unsigned long>(position), static_cast<unsigned long>(total));
  const int textLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, text, EpdFontFamily::REGULAR);
  const int innerWidth = std::max(textWidth, minInnerWidth);
  const int boxWidth = innerWidth + paddingX * 2;
  const int boxHeight = paddingY * 2 + textLineHeight + barGap + barHeight;
  const int boxX = std::max(safeInset, screenWidth - safeInset - boxWidth);
  const int boxY = std::max(safeInset, screenHeight - boxHeight - safeInset);

  renderer.fillRect(boxX, boxY, boxWidth, boxHeight, true);
  if (renderer.getRenderMode() == GfxRenderer::BW) {
    renderer.drawRect(boxX, boxY, boxWidth, boxHeight, 1, false);
    const int textX = boxX + paddingX + (innerWidth - textWidth) / 2;
    renderer.drawText(UI_10_FONT_ID, textX, boxY + paddingY, text, false, EpdFontFamily::REGULAR);
    const int barX = boxX + paddingX;
    const int barY = boxY + paddingY + textLineHeight + barGap;
    renderer.drawRect(barX, barY, innerWidth, barHeight, 1, false);
    int fillWidth = static_cast<int>(static_cast<uint64_t>(position) * innerWidth / total);  // caller ensures total > 0
    if (fillWidth > innerWidth) fillWidth = innerWidth;
    if (fillWidth > 0) renderer.fillRect(barX, barY, fillWidth, barHeight, false);
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
    drawPositionBadge(renderer, g_position, g_total);
  }
}
