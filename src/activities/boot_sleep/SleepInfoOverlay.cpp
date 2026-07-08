#include "SleepInfoOverlay.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "fontIds.h"
#include "util/FavoriteImage.h"

namespace {

// Filled box + text in the bottom-left safe corner. White text on a black box so
// it reads over any wallpaper. Mirrors the DX34 geometry.
void drawLabel(const GfxRenderer& renderer, const std::string& text) {
  if (text.empty()) return;
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int safeInset = 18;
  const int paddingX = 4;
  const int paddingY = 2;
  const int textLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int maxBoxWidth = std::max(1, screenWidth - safeInset * 2);
  const int maxTextWidth = std::max(1, maxBoxWidth - paddingX * 2 - 2);

  const std::string shown = renderer.truncatedText(UI_10_FONT_ID, text.c_str(), maxTextWidth);
  const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, shown.c_str(), EpdFontFamily::REGULAR);
  const int boxWidth = std::min(textWidth + paddingX * 2, maxBoxWidth);
  const int boxHeight = textLineHeight + paddingY * 2;
  const int boxX = safeInset;
  const int boxY = std::max(safeInset, screenHeight - boxHeight - safeInset);

  renderer.fillRect(boxX, boxY, boxWidth, boxHeight, true);
  renderer.drawRect(boxX, boxY, boxWidth, boxHeight, 1, false);
  renderer.drawText(UI_10_FONT_ID, boxX + paddingX, boxY + paddingY, shown.c_str(), false, EpdFontFamily::REGULAR);
}

}  // namespace

void drawSleepInfoOverlay(const GfxRenderer& renderer, const std::string& sourcePath) {
  if (sourcePath.empty()) return;

  if (SETTINGS.showSleepImageFilename) {
    drawLabel(renderer, FavoriteImage::displayNameForPath(sourcePath));
  } else if (SETTINGS.showSleepFavoriteBadge && FavoriteImage::isFavoritePath(sourcePath)) {
    // Just "F" — the box border reads as the brackets.
    drawLabel(renderer, "F");
  }
}
