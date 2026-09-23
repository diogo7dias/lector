#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "PxcSleepRenderer.h"
#include "fontIds.h"
#include "util/DebugTrace.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  // Redraw the wallpaper in one 1-bit HALF refresh, without banners. The retained
  // sleep image stays on the panel until this paint; missing/corrupt files fall back
  // to the centred name below. No top banner remains to justify the old FULL pass.
  // Sleep info badges occupy bottom corners only. HALF redraws those pixels too:
  // X4 uses its clean 0xD7 sequence; an explicit HALF on X3 requests a resync. Local
  // charge residue still needs a panel check, but does not justify a FULL branch.
  if (!wallpaperPath_.empty() &&
      renderPxcSleepScreen(renderer, wallpaperPath_, /*grayscale=*/false, HalDisplay::HALF_REFRESH)) {
    return;
  }

  debug_trace::note("boot face: Lector, wallpaper=%s", wallpaperPath_.empty() ? "none" : "failed");
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  // The same face the sleep screen falls back to: the name, centred, in the one UI face.
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  renderer.drawCenteredText(UI_10_FONT_ID, (pageHeight - lineHeight) / 2, tr(STR_LECTOR));
  renderer.displayBuffer();
}
