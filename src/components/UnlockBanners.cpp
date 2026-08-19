#include "UnlockBanners.h"

#include <GfxRenderer.h>

#include <cctype>
#include <string>
#include <vector>

#include "BannerStyle.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "WakeTiming.h"
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

// Empty means "name APP_STATE.openEpubPath". See setUnlockBannerBookPath().
std::string bannerBookPathOverride;

// One draw for both entry points below: the top banner is identical either way, and the
// footer band is what the sleep face leaves out.
void drawBanners(GfxRenderer& renderer, bool withFooter);

}  // namespace

void setUnlockBannerBookPath(const std::string& path) { bannerBookPathOverride = path; }

// The top banner alone: version plus the book being resumed. Split out of
// drawUnlockBanners() for the Light sleep face, which names the book it is about to
// open but has no use for the footer band at the bottom of a sleeping screen.
void drawUnlockBannerTop(GfxRenderer& renderer) { drawBanners(renderer, /*withFooter=*/false); }

void drawUnlockBanners(GfxRenderer& renderer) { drawBanners(renderer, /*withFooter=*/true); }

namespace {

void drawBanners(GfxRenderer& renderer, const bool withFooter) {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const bool bwPass = renderer.getRenderMode() == GfxRenderer::BW;

  // Shared with the message popups (BannerStyle.h) so every black banner in the
  // firmware is the same band: same font, same padding, same rule.
  const int pad = banner::PAD;
  const int lh10 = renderer.getLineHeight(banner::FONT_ID);

  // The book this boot is actually heading into: the override when one was set (random
  // book on boot), otherwise the book being resumed.
  const std::string& bannerBookPath = bannerBookPathOverride.empty() ? APP_STATE.openEpubPath : bannerBookPathOverride;

  std::string bookLine;
  if (!bannerBookPath.empty()) {
    bookLine = bookTitleFromPath(bannerBookPath);
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
  if (withFooter) renderer.fillRect(0, botY, pageWidth, botH, true);  // black banner, drawn every pass

  // White content (border + text) only in the BW base pass: the 1-bit draw path
  // ignores the render mode and would set the grayscale plane bits (a dark-grey nudge)
  // in the LSB/MSB passes, greying the white pixels.
  if (!bwPass) return;

  // One rule per banner, on the edge that faces the page: the banners span the screen
  // and reach its physical edges, so a full frame just boxes in a band.
  constexpr int rule = banner::RULE;
  renderer.fillRect(0, topY + topH - rule, pageWidth, rule, false);    // under the top banner
  if (withFooter) renderer.fillRect(0, botY, pageWidth, rule, false);  // over the bottom banner

  // The version string already names the firmware ("lector.c 0.0.8"), so prefixing it
  // with "Lector " read as "Lector lector.c 0.0.8".
  renderer.drawCenteredText(banner::FONT_ID, topY + pad, CROSSPOINT_VERSION, false);
  int titleY = topY + pad + lh10 + 4;
  for (const std::string& line : titleLines) {
    renderer.drawCenteredText(banner::FONT_ID, titleY, line.c_str(), false);
    titleY += lh10;
  }

  if (!withFooter) return;

  const char* footer = SETTINGS.customFooter[0] != '\0' ? SETTINGS.customFooter : "READ UNTIL YOU DIE.";
#if defined(WAKE_TIMING_OVERLAY) && WAKE_TIMING_OVERLAY
  // Experimental builds only: replace the footer with the previous wake's breakdown, so a
  // device with no serial console can still report where its wake time went. The stamps
  // are one wake behind by necessity (see WakeTiming.h), which is why this reads as a
  // measurement of the last unlock rather than of this one.
  // formatDiagnostic, not formatPrevious: an empty result used to fall through to the
  // normal footer, which reads exactly like a build with the overlay switched off. The
  // diagnostic always prints something, so "no numbers" becomes a readable cause.
  char timings[96];
  WakeTiming::formatDiagnostic(timings, sizeof(timings));
  if (timings[0] != '\0') footer = timings;
#endif
  renderer.drawCenteredText(banner::FONT_ID, botY + pad, footer, false);
}

}  // namespace
