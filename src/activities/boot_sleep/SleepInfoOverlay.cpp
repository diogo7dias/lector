#include "SleepInfoOverlay.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "fontIds.h"
#include "util/FavoriteImage.h"

namespace {

// The wallpaper currently being rendered, owned by SleepInfoOverlayScope. Empty
// means "no wallpaper in scope", which disables the overlay entirely.
std::string g_sourcePath;

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

}  // namespace

SleepInfoOverlayScope::SleepInfoOverlayScope(const std::string& sourcePath) { g_sourcePath = sourcePath; }

SleepInfoOverlayScope::~SleepInfoOverlayScope() { g_sourcePath.clear(); }

void drawSleepInfoOverlay(GfxRenderer& renderer) {
  if (g_sourcePath.empty()) return;

  if (SETTINGS.showSleepImageFilename) {
    drawLabel(renderer, FavoriteImage::displayNameForPath(g_sourcePath));
  } else if (SETTINGS.showSleepFavoriteBadge && FavoriteImage::isFavoritePath(g_sourcePath)) {
    // Just "F" — the box border reads as the brackets.
    drawLabel(renderer, "F");
  }
}
