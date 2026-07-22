#include "BootActivity.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>

#include <cctype>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "PxcSleepRenderer.h"
#include "components/TopEdgeInset.h"
#include "fontIds.h"

namespace {

// "/books/My Book.epub" -> "My Book". Strips the directory and the extension so
// the resuming-book banner reads as a plain title.
std::string bookTitleFromPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const size_t start = (slash == std::string::npos) ? 0 : slash + 1;
  size_t end = path.find_last_of('.');
  if (end == std::string::npos || end < start) end = path.size();
  return path.substr(start, end - start);
}

}  // namespace

void BootActivity::drawUnlockBanners() const {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const bool bwPass = renderer.getRenderMode() == GfxRenderer::BW;

  const int pad = 10;
  const int lh12 = renderer.getLineHeight(UI_12_FONT_ID);
  const int lh10 = renderer.getLineHeight(UI_10_FONT_ID);

  std::string bookLine;
  if (!APP_STATE.openEpubPath.empty()) {
    bookLine = bookTitleFromPath(APP_STATE.openEpubPath);
    for (char& c : bookLine) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }

  // Title wraps to at most 12 rows so a long title keeps its words instead of a
  // single ellipsised line. The version and every title row share the UI_10 size.
  std::vector<std::string> titleLines;
  if (!bookLine.empty()) {
    titleLines = renderer.wrappedText(UI_10_FONT_ID, bookLine.c_str(), pageWidth - 24, 12);
  }
  const int titleRows = static_cast<int>(titleLines.size());

  // --- TOP banner: version (+ resuming book, up to 12 title rows) ---
  const int topH = pad * 2 + lh10 + (titleRows > 0 ? 4 + titleRows * lh10 : 0);
  const int topY = topEdgeInset(gpio.deviceIsX4());
  renderer.fillRect(0, 0, pageWidth, topH + topY, true);  // black backing reaches the physical edge

  // --- BOTTOM banner: footer text ---
  const int botH = lh12 + pad * 2;
  const int botY = pageHeight - botH;
  renderer.fillRect(0, botY, pageWidth, botH, true);  // black banner, drawn every pass

  // White content (border + text) only in the BW base pass: the 1-bit draw path
  // ignores the render mode and would set the grayscale plane bits (a dark-grey
  // nudge) in the LSB/MSB passes, greying the white pixels.
  if (!bwPass) return;

  renderer.drawRect(0, topY, pageWidth, topH, 2, false);  // 2px white inset border
  renderer.drawRect(0, botY, pageWidth, botH, 2, false);

  // Dark/paperback text: smear the white glyphs +1px so they read heavier on the
  // black banners (matches drawPopup). Restored after so nothing else is affected.
  renderer.setPaperbackLook(true);

  const std::string version = std::string("Lector ") + CROSSPOINT_VERSION;
  renderer.drawCenteredText(UI_10_FONT_ID, topY + pad, version.c_str(), false);
  int titleY = topY + pad + lh10 + 4;
  for (const std::string& line : titleLines) {
    renderer.drawCenteredText(UI_10_FONT_ID, titleY, line.c_str(), false);
    titleY += lh10;
  }

  const char* footer = SETTINGS.customFooter[0] != '\0' ? SETTINGS.customFooter : "READ UNTIL YOU DIE.";
  renderer.drawCenteredText(UI_12_FONT_ID, botY + pad, footer, false);

  renderer.setPaperbackLook(false);
}

void BootActivity::onEnter() {
  Activity::onEnter();

  // Unlock over the wallpaper: redraw the .pxc as a single 1-bit refresh with the
  // banners composited on top, so wake stays fast (the 3-pass grayscale re-render
  // is the slow part and X3 has no partial-refresh shortcut). The seamless begin()
  // in setup() kept the wallpaper physically on the panel until this refresh lands,
  // so there is no white flash. Falls back to the plain banner screen if the
  // wallpaper can't be re-rendered (missing / corrupt file).
  if (!wallpaperPath_.empty()) {
    if (renderPxcSleepScreen(
            renderer, wallpaperPath_, [this]() { drawUnlockBanners(); }, /*drawInfoOverlay=*/false,
            /*grayscale=*/false)) {
      return;
    }
  }

  // Plain banner screen (cold boot / logo modes / non-pxc wallpaper): white
  // background with the same two banners. renderMode is BW here so all banner
  // content draws.
  renderer.clearScreen();
  drawUnlockBanners();
  renderer.present(RefreshIntent::CleanFrame);
}
