#include "BootActivity.h"

#include <GfxRenderer.h>

#include "LectorFace.h"
#include "PxcSleepRenderer.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  // Redraw the wallpaper in one 1-bit HALF refresh, without banners. The retained
  // sleep image stays on the panel until this paint; missing/corrupt files fall back
  // to the crow face below. No top banner remains to justify the old FULL pass.
  // Sleep info badges occupy bottom corners only. HALF redraws those pixels too:
  // X4 uses its clean 0xD7 sequence; an explicit HALF on X3 requests a resync. Local
  // charge residue still needs a panel check, but does not justify a FULL branch.
  if (!wallpaperPath_.empty() &&
      renderPxcSleepScreen(renderer, wallpaperPath_, /*grayscale=*/false, HalDisplay::HALF_REFRESH)) {
    return;
  }

  renderer.clearScreen();
  // The same face the sleep screen falls back to.
  drawLectorFace(renderer);
  // HALF for the same reason as the wallpaper: a FAST pass leaves the retained frame under the face.
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
