#include "UnlockBanners.h"

#include <GfxRenderer.h>

#include <cctype>
#include <string>
#include <vector>

#include "BannerStyle.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "fontIds.h"

namespace {

// "/books/My Book.epub" -> "My Book". Strips the directory and the extension so the
// resuming-book banner reads as a plain title.
std::string bookTitleFromPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const size_t start = (slash == std::string::npos) ? 0 : slash + 1;
  size_t end = path.find_last_of('.');
  if (end == std::string::npos || end < start) end = path.size();
  return path.substr(start, end - start);
}

}  // namespace

void drawUnlockBanners(GfxRenderer& renderer) {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const bool bwPass = renderer.getRenderMode() == GfxRenderer::BW;

  // Shared with the message popups (BannerStyle.h) so every black banner in the
  // firmware is the same band: same font, same padding, same rule.
  const int pad = banner::PAD;
  const int lh10 = renderer.getLineHeight(banner::FONT_ID);

  std::string bookLine;
  if (!APP_STATE.openEpubPath.empty()) {
    bookLine = bookTitleFromPath(APP_STATE.openEpubPath);
    for (char& c : bookLine) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }

  // Title wraps to at most 12 rows so a long title keeps its words instead of a single
  // ellipsised line. The version and every title row share the banner font size.
  std::vector<std::string> titleLines;
  if (!bookLine.empty()) {
    titleLines = renderer.wrappedText(banner::FONT_ID, bookLine.c_str(), pageWidth - 24, 12);
  }
  const int titleRows = static_cast<int>(titleLines.size());

  // Physical top crop (X4 crops ~9px; X3 = 0) via the renderer's oriented viewable
  // inset, so the black backing reaches the physical edge while the border/text sit
  // below the crop. Uses the global mechanism, not a per-site device inset.
  int viewTop = 0, viewRight = 0, viewBottom = 0, viewLeft = 0;
  renderer.getOrientedViewableTRBL(&viewTop, &viewRight, &viewBottom, &viewLeft);

  // --- TOP banner: version (+ resuming book, up to 12 title rows) ---
  const int topH = pad * 2 + lh10 + (titleRows > 0 ? 4 + titleRows * lh10 : 0);
  const int topY = viewTop;
  renderer.fillRect(0, 0, pageWidth, topH + topY, true);  // black backing reaches the physical edge

  // --- BOTTOM banner: footer text ---
  const int botH = lh10 + pad * 2;
  const int botY = pageHeight - botH;
  renderer.fillRect(0, botY, pageWidth, botH, true);  // black banner, drawn every pass

  // White content (border + text) only in the BW base pass: the 1-bit draw path
  // ignores the render mode and would set the grayscale plane bits (a dark-grey nudge)
  // in the LSB/MSB passes, greying the white pixels.
  if (!bwPass) return;

  // One rule per banner, on the edge that faces the page: the banners span the screen
  // and reach its physical edges, so a full frame just boxes in a band.
  constexpr int rule = banner::RULE;
  renderer.fillRect(0, topY + topH - rule, pageWidth, rule, false);  // under the top banner
  renderer.fillRect(0, botY, pageWidth, rule, false);                // over the bottom banner

  // The version string already names the firmware ("lector.c 0.0.8"), so prefixing it
  // with "Lector " read as "Lector lector.c 0.0.8".
  renderer.drawCenteredText(banner::FONT_ID, topY + pad, CROSSPOINT_VERSION, false);
  int titleY = topY + pad + lh10 + 4;
  for (const std::string& line : titleLines) {
    renderer.drawCenteredText(banner::FONT_ID, titleY, line.c_str(), false);
    titleY += lh10;
  }

  const char* footer = SETTINGS.customFooter[0] != '\0' ? SETTINGS.customFooter : "READ UNTIL YOU DIE.";
  renderer.drawCenteredText(banner::FONT_ID, botY + pad, footer, false);
}
