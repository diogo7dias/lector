#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "components/UnlockBanners.h"
#include "fontIds.h"
#include "images/Logo120.h"

void BootActivity::onEnter() {
  Activity::onEnter();

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
