#include "SleepInfoOverlay.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>

#include "CrossPointSettings.h"
#include "UiFont.h"
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

// A paper label in a bottom safe corner: the text in italic, black on white inside a
// hairline frame, so it reads over any wallpaper. Every badge shares this shape.
void drawLabel(const GfxRenderer& renderer, const std::string& text, const Corner corner = Corner::BottomLeft) {
  if (text.empty()) return;
  const int font = uiLanguageNeedsUbuntu() ? UI_10_FONT_ID : LITERATA_UI_19_IT_FONT_ID;
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  constexpr int safeInset = 18;
  constexpr int paddingX = 10;
  constexpr int paddingY = 2;
  const int textLineHeight = renderer.getLineHeight(font);
  const int maxBoxWidth = std::max(1, screenWidth - safeInset * 2);
  const int maxTextWidth = std::max(1, maxBoxWidth - paddingX * 2 - 2);

  const std::string shown = renderer.truncatedText(font, text.c_str(), maxTextWidth);
  const int textWidth = renderer.getTextWidth(font, shown.c_str(), EpdFontFamily::REGULAR);
  const int boxWidth = std::min(textWidth + paddingX * 2, maxBoxWidth);
  const int boxHeight = textLineHeight + paddingY * 2;
  const int boxX = corner == Corner::BottomRight ? std::max(safeInset, screenWidth - safeInset - boxWidth) : safeInset;
  const int boxY = std::max(safeInset, screenHeight - boxHeight - safeInset);

  // In the grayscale plane passes fillRect(true) clears the plane bits over the box, which
  // stops the wallpaper's gray nudges bleeding through the paper. The paper, frame and
  // text belong to the BW base pass only: the 1-bit glyph/rect path ignores the render
  // mode and would set the plane bits, turning black ink into the dark-grey nudge cell.
  if (renderer.getRenderMode() != GfxRenderer::BW) {
    renderer.fillRect(boxX, boxY, boxWidth, boxHeight, true);
    return;
  }
  renderer.fillRect(boxX, boxY, boxWidth, boxHeight, false);
  renderer.drawRect(boxX, boxY, boxWidth, boxHeight, 1, true);
  renderer.drawText(font, boxX + paddingX, boxY + paddingY, shown.c_str(), true, EpdFontFamily::REGULAR);
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
