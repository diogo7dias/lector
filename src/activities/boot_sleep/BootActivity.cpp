#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "PxcSleepRenderer.h"
#include "components/UnlockBanners.h"
#include "fontIds.h"
#include "images/Logo120.h"

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

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_CROSSPOINT), true, EpdFontFamily::REGULAR);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_BOOTING));
  // Framed top and bottom banners, same as the quick-resume wake path. Only a
  // quick-resume sleep keeps a saved frame to composite over, and every other sleep
  // screen (wallpaper, cover, dark) wakes through here — without this the banners
  // never appeared for those. The top banner carries the version, so the standalone
  // version line this used to draw at the bottom would only be covered by the bottom
  // banner anyway.
  drawUnlockBanners(renderer);
  renderer.displayBuffer();
}
