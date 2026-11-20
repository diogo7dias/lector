#pragma once

#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <I18n.h>

#include <cstdint>
#include <cstdio>

#include "LightPanelGeometry.h"
#include "fontIds.h"

// The light panel's drawing that needs none of its state: the shade over the page below the
// band, and the battery and free-space line at its foot.
namespace light_panel {

// One pixel in two, on a checkerboard: dark enough to read as a shade over the page and
// sparse enough to leave the words under it legible. Only black pixels are written, so
// the page is darkened rather than replaced — which is why this is a loop and not
// fillRectDither(LightGray), whose white pixels would erase it.
inline void stipplePageBelow(const GfxRenderer& renderer, const int bandHeight) {
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  for (int y = bandHeight; y < height; ++y) {
    for (int x = (y & 1); x < width; x += 2) renderer.drawPixel(x, y, true);
  }
}

// Battery and free space, in the small UI font. No rule above it: the button grid it
// follows is already a row of boxes, and a line under those read as a second border.
inline void drawReadout(const GfxRenderer& renderer, const Rect& rect, const int topInset, const uint64_t freeBytes) {
  const int y = rect.y + topInset;

  char left[32];
  snprintf(left, sizeof(left), "%s %u%%", I18N.get(StrId::STR_BATTERY),
           static_cast<unsigned>(powerManager.getBatteryPercentage()));
  renderer.drawText(UI_10_FONT_ID, rect.x, y, left, true);

  char right[32];
  // Whole gigabytes below ten get a decimal; above it the tenth is noise on a card this
  // size and the shorter string is easier to read at a glance.
  const double gigabytes = static_cast<double>(freeBytes) / (1024.0 * 1024.0 * 1024.0);
  if (gigabytes < 10.0) {
    snprintf(right, sizeof(right), "%.1f GB %s", gigabytes, I18N.get(StrId::STR_FREE_SPACE));
  } else {
    snprintf(right, sizeof(right), "%d GB %s", static_cast<int>(gigabytes + 0.5), I18N.get(StrId::STR_FREE_SPACE));
  }
  const int width = renderer.getTextWidth(UI_10_FONT_ID, right);
  renderer.drawText(UI_10_FONT_ID, rect.x + rect.width - width, y, right, true);
}

}  // namespace light_panel
