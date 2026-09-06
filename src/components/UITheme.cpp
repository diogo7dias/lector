#include "UITheme.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <Logging.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/StatusBar.h"  // statusBarThicknessPx for the v2 height helpers
#include "components/themes/BaseTheme.h"
#include "components/themes/TouchMetrics.h"

UITheme UITheme::instance;

UITheme::UITheme() {
  // Lector is the only look. All of it lives in BaseTheme / BaseMetrics.
  currentMetrics = &BaseMetrics::values;
}

const ThemeMetrics& UITheme::getMetrics() const {
  // A touch board gets finger-sized interactive bands; a button-only board keeps the
  // metrics exactly as the theme declares them. Cached because getMetrics() is called
  // several times per frame, and re-derived when the answer to hasTouch() changes (it
  // does once, at boot, after the panel is probed).
  const bool touch = gpio.hasTouch();
  if (!metricsValid || metricsForTouch != touch) {
    adjustedMetrics = touch ? touch_metrics::adjusted(*currentMetrics) : *currentMetrics;
    metricsForTouch = touch;
    metricsValid = true;
  }
  return adjustedMetrics;
}

// Screen area excluding the button hints
Rect UITheme::getScreenSafeArea(const GfxRenderer& renderer, bool hasFrontButtonHints, bool hasSideButtonHints) {
  auto orientation = renderer.getOrientation();
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  Rect safeArea = Rect{0, 0, screenWidth, screenHeight};
  const ThemeMetrics metrics = getMetrics();
  switch (orientation) {
    case GfxRenderer::Orientation::Portrait:
      if (hasFrontButtonHints) {
        safeArea.height -= metrics.buttonHintsHeight;
      }
      break;
    case GfxRenderer::Orientation::LandscapeClockwise:
      if (hasFrontButtonHints) {
        safeArea.x += metrics.buttonHintsHeight;
        safeArea.width -= metrics.buttonHintsHeight;
      }
      break;
    case GfxRenderer::Orientation::PortraitInverted:
      if (hasFrontButtonHints) {
        safeArea.y += metrics.buttonHintsHeight;
        safeArea.height -= metrics.buttonHintsHeight;
      }
      break;
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
      if (hasFrontButtonHints) {
        safeArea.width -= metrics.buttonHintsHeight;
      }
      break;
  }
  return safeArea;
}

std::string UITheme::getCoverThumbPath(std::string coverBmpPath, int coverHeight) {
  size_t pos = coverBmpPath.find("[HEIGHT]", 0);
  if (pos != std::string::npos) {
    coverBmpPath.replace(pos, 8, std::to_string(coverHeight));
  }
  return coverBmpPath;
}

UIIcon UITheme::getFileIcon(const std::string& filename) {
  if (filename.back() == '/') {
    return Folder;
  }
  if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename)) {
    return Book;
  }
  if (FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename)) {
    return Text;
  }
  if (FsHelpers::hasBmpExtension(filename) || FsHelpers::hasPngExtension(filename)) {
    return Image;
  }
  return File;
}

int UITheme::getProgressBarHeight() {
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  const bool showProgressBar =
      SETTINGS.progressBarsVisible() && (SETTINGS.sbBookBar != CrossPointSettings::SB_EDGE_OFF ||
                                         SETTINGS.sbChapterBar != CrossPointSettings::SB_EDGE_OFF);
  return showProgressBar ? (statusBarDrawThicknessPx(SETTINGS.activeBarThickness(), SETTINGS.sbBarOutline != 0) +
                            metrics.progressBarMarginTop + SETTINGS.floatingBarMarginPx())
                         : 0;
}

namespace {
bool sbAnchorTop(uint8_t a) {
  return a == CrossPointSettings::SB_ANCHOR_TL || a == CrossPointSettings::SB_ANCHOR_TC ||
         a == CrossPointSettings::SB_ANCHOR_TR;
}
bool sbAnchorBottom(uint8_t a) {
  return a == CrossPointSettings::SB_ANCHOR_BL || a == CrossPointSettings::SB_ANCHOR_BC ||
         a == CrossPointSettings::SB_ANCHOR_BR;
}
bool sbItemOn(uint8_t anchor, bool chapterOnly, bool hasChapters) {
  return anchor != CrossPointSettings::SB_ANCHOR_OFF && (!chapterOnly || hasChapters);
}
// Whether any enabled text item lands on the requested band (top=true / bottom).
// NOTE: this is the *native* anchor assignment; the rare title-driven reflow that
// pushes a top item down to the bottom band is not reflected here (device-tuned
// later), so a reserved band never disappears — at worst a bumped item may draw in
// a band that was already reserved for its native residents.
bool sbBandHasText(bool top, bool hasChapters) {
  const bool clockAvailable = halClock.isAvailable();
  const struct {
    uint8_t anchor;
    bool chapterOnly;
    bool applicable;
  } items[] = {
      {SETTINGS.sbBatteryPos, false, true},   {SETTINGS.sbClockPos, false, clockAvailable},
      {SETTINGS.sbTitlePos, false, true},  // title falls back to book title on chapterless books
      {SETTINGS.sbPagePos, false, true},   // page falls back to book pages on chapterless books
      {SETTINGS.sbBookPctPos, false, true},   {SETTINGS.sbChapterPctPos, true, true},
      {SETTINGS.sbChapterNumPos, true, true}, {SETTINGS.sbSessionPagesPos, false, true},
      {SETTINGS.sbParaPagesPos, false, true},
  };
  for (const auto& it : items) {
    if (!it.applicable) continue;
    if (!sbItemOn(it.anchor, it.chapterOnly, hasChapters)) continue;
    if (top ? sbAnchorTop(it.anchor) : sbAnchorBottom(it.anchor)) return true;
  }
  return false;
}
}  // namespace

int UITheme::getStatusBarV2TopHeight(bool hasChapters, int extraTitleHeightPx) {
  // The bars can outlive the text: with the status bar hidden and sbOffBar set, this
  // band reserves the bar only. See CrossPointSettings::progressBarsVisible().
  if (!SETTINGS.progressBarsVisible()) return 0;
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  const int barPx = statusBarDrawThicknessPx(SETTINGS.activeBarThickness(), SETTINGS.sbBarOutline != 0);
  int bars = 0;
  if (SETTINGS.sbBookBar == CrossPointSettings::SB_EDGE_TOP) bars += barPx;
  if (SETTINGS.sbChapterBar == CrossPointSettings::SB_EDGE_TOP && hasChapters) bars += barPx;
  const bool hasText = SETTINGS.statusBarEnabled() && sbBandHasText(true, hasChapters);
  const int text = hasText ? metrics.statusBarVerticalMargin + (extraTitleHeightPx > 0 ? extraTitleHeightPx : 0) : 0;
  // The floating margin is the gap ABOVE the topmost bar, so it is paid once per
  // band, not once per bar.
  return text + (bars > 0 ? bars + metrics.progressBarMarginTop + SETTINGS.floatingBarMarginPx() : 0);
}

int UITheme::getStatusBarV2BottomHeight(bool hasChapters, int extraTitleHeightPx) {
  if (!SETTINGS.progressBarsVisible()) return 0;
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  const int barPx = statusBarDrawThicknessPx(SETTINGS.activeBarThickness(), SETTINGS.sbBarOutline != 0);
  int bars = 0;
  if (SETTINGS.sbBookBar == CrossPointSettings::SB_EDGE_BOTTOM) bars += barPx;
  if (SETTINGS.sbChapterBar == CrossPointSettings::SB_EDGE_BOTTOM && hasChapters) bars += barPx;
  const bool hasText = SETTINGS.statusBarEnabled() && sbBandHasText(false, hasChapters);
  const int text = hasText ? metrics.statusBarVerticalMargin + (extraTitleHeightPx > 0 ? extraTitleHeightPx : 0) : 0;
  return text + (bars > 0 ? bars + metrics.progressBarMarginTop + SETTINGS.floatingBarMarginPx() : 0);
}

int UITheme::getStatusBarV2BandWidth(const GfxRenderer& renderer) {
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  int mt, mr, mb, ml;
  renderer.getOrientedViewableTRBL(&mt, &mr, &mb, &ml);
  const int leftEdge = metrics.statusBarHorizontalMargin + ml + 1;
  const int rightEdge = renderer.getScreenWidth() - metrics.statusBarHorizontalMargin - mr;
  return rightEdge - leftEdge;
}

int UITheme::getStatusBarV2TitleLines(const GfxRenderer& renderer, const char* title) {
  if (!SETTINGS.statusBarEnabled() || SETTINGS.sbTitlePos == CrossPointSettings::SB_ANCHOR_OFF) return 1;
  if (SETTINGS.sbTitleTruncate != 0) return 1;  // a clipping title stays one line
  if (!title || title[0] == '\0') return 1;
  const int bandWidth = getStatusBarV2BandWidth(renderer);
  if (bandWidth <= 0) return 1;
  // Safety ceiling: real book/chapter titles never approach this many UI_10 lines,
  // but it bounds the reserved band (and the render loop) for a pathological title.
  constexpr int kMaxTitleLines = 6;
  const int lines = static_cast<int>(renderer.wrappedText(UI_10_FONT_ID, title, bandWidth, kMaxTitleLines).size());
  return lines < 1 ? 1 : lines;
}

// Centered text implementation that takes the safe area into account
void UITheme::drawCenteredText(const GfxRenderer& renderer, Rect screen, int fontId, int y, const char* text,
                               bool black, EpdFontFamily::Style style) {
  const int x = screen.x + (screen.width - renderer.getTextWidth(fontId, text, style)) / 2;
  renderer.drawText(fontId, x, y, text, black, style);
}
