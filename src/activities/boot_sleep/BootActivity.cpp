#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointState.h"
#include "PxcSleepRenderer.h"
#include "components/UnlockBanners.h"
#include "fontIds.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  // Unlock over the wallpaper: redraw the .pxc as a single 1-bit refresh with the
  // banners composited on top, so the wallpaper the user fell asleep to is still there
  // on wake. 1-bit rather than the 3-pass grayscale pipeline because wake speed matters
  // more here than tone, and the seamless display begin() in setup() kept the wallpaper
  // physically on the panel until this refresh lands, so there is no white flash.
  // Falls through to the logo screen if the file is missing or corrupt.
  if (!wallpaperPath_.empty() && renderPxcSleepScreen(renderer, wallpaperPath_, /*grayscale=*/false,
                                                      HalDisplay::HALF_REFRESH, &drawUnlockBanners)) {
    return;
  }

  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  // The same face the sleep screen falls back to: the name, centred, in the one UI face.
  // The top banner already spells out the firmware and version.
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  renderer.drawCenteredText(UI_10_FONT_ID, (pageHeight - lineHeight) / 2, tr(STR_LECTOR));
  // Framed top and bottom banners: the top one carries the version and the book about to
  // open, the bottom one the user's footer line.
  drawUnlockBanners(renderer);
  renderer.displayBuffer();
}
