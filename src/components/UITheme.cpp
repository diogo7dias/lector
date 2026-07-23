#include "UITheme.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <Logging.h>

#include <memory>

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/StatusBar.h"
#include "components/TopEdgeInset.h"
#include "components/themes/BaseTheme.h"
#include "components/themes/lector/LectorTheme.h"
#include "fontIds.h"

UITheme UITheme::instance;

UITheme::UITheme() {
  auto themeType = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  setTheme(themeType);
}

void UITheme::reload() {
  auto themeType = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  setTheme(themeType);
}

void UITheme::setTheme(CrossPointSettings::UI_THEME /*type*/) {
  // Lector is the only theme (BaseTheme look). Any persisted uiTheme value resolves
  // here; a stale index from a removed theme simply lands on Lector.
  LOG_DBG("UI", "Using Lector theme");
  currentTheme = std::make_unique<LectorTheme>();
  // Fold the X4 top-edge crop into topPadding so every chrome screen (header,
  // tab bars, sub-headers, list/content areas — all derived from topPadding)
  // shifts down together on X4 and matches X3's layout. Rebuilt here so a boot
  // reload() after device detection picks up the correct device.
  deviceMetrics_ = BaseMetrics::values;
  deviceMetrics_.topPadding = chromeTopPadding(BaseMetrics::values.topPadding, gpio.deviceIsX4());
  currentMetrics = &deviceMetrics_;
}

int UITheme::getNumberOfItemsPerPage(const GfxRenderer& renderer, bool hasHeader, bool hasTabBar, bool hasButtonHints,
                                     bool hasSubtitle, int extraReservedHeight) {
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  auto orientation = renderer.getOrientation();
  int reservedHeight = metrics.topPadding;
  if (hasHeader) {
    reservedHeight += metrics.headerHeight + metrics.verticalSpacing;
  }
  if (hasTabBar) {
    reservedHeight += metrics.tabBarHeight;
  }
  if (hasButtonHints && orientation != GfxRenderer::Orientation::LandscapeClockwise &&
      orientation != GfxRenderer::Orientation::LandscapeCounterClockwise) {
    reservedHeight += metrics.verticalSpacing + metrics.buttonHintsHeight;
  }
  const int availableHeight = renderer.getScreenHeight() - reservedHeight - extraReservedHeight;
  int rowHeight = hasSubtitle ? metrics.listWithSubtitleRowHeight : metrics.listRowHeight;
  return availableHeight / rowHeight;
}

// Screen area excluding the button hints
Rect UITheme::getScreenSafeArea(const GfxRenderer& renderer, bool hasFrontButtonHints, bool hasSideButtonHints) {
  auto orientation = renderer.getOrientation();
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  Rect safeArea = Rect{0, 0, screenWidth, screenHeight};
  switch (orientation) {
    case GfxRenderer::Orientation::Portrait:
      if (hasFrontButtonHints) {
        safeArea.height -= currentMetrics->buttonHintsHeight;
      }
      break;
    case GfxRenderer::Orientation::LandscapeClockwise:
      if (hasFrontButtonHints) {
        safeArea.x += currentMetrics->buttonHintsHeight;
        safeArea.width -= currentMetrics->buttonHintsHeight;
      }
      break;
    case GfxRenderer::Orientation::PortraitInverted:
      if (hasFrontButtonHints) {
        safeArea.y += currentMetrics->buttonHintsHeight;
        safeArea.height -= currentMetrics->buttonHintsHeight;
      }
      break;
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
      if (hasFrontButtonHints) {
        safeArea.width -= currentMetrics->buttonHintsHeight;
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
  if (FsHelpers::hasBmpExtension(filename)) {
    return Image;
  }
  return File;
}

// Legacy height helpers, now expressed over the v2 per-item model. Still used by
// the EPUB auto-page-turn logic to decide whether a text lane exists (vs only a
// progress bar / nothing) so it can reserve space for the countdown indicator.
int UITheme::getStatusBarHeight() {
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  const bool showText = SETTINGS.sbEnabled &&
                        (SETTINGS.sbBatteryPos || SETTINGS.sbClockPos || SETTINGS.sbTitlePos || SETTINGS.sbPagePos ||
                         SETTINGS.sbBookPctPos || SETTINGS.sbChapterPctPos || SETTINGS.sbChapterNumPos);
  return (showText ? metrics.statusBarVerticalMargin : 0) + getProgressBarHeight();
}

int UITheme::getProgressBarHeight() {
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  const bool showProgressBar = SETTINGS.sbEnabled && (SETTINGS.sbBookBar != CrossPointSettings::SB_EDGE_OFF ||
                                                      SETTINGS.sbChapterBar != CrossPointSettings::SB_EDGE_OFF);
  return showProgressBar ? (statusBarThicknessPx(SETTINGS.sbBarThickness) + metrics.progressBarMarginTop) : 0;
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
      {SETTINGS.sbChapterNumPos, true, true},
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
  if (!SETTINGS.sbEnabled) return 0;
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  const int barPx = statusBarThicknessPx(SETTINGS.sbBarThickness);
  int bars = 0;
  if (SETTINGS.sbBookBar == CrossPointSettings::SB_EDGE_TOP) bars += barPx;
  if (SETTINGS.sbChapterBar == CrossPointSettings::SB_EDGE_TOP && hasChapters) bars += barPx;
  const bool hasText = sbBandHasText(true, hasChapters);
  const int text = hasText ? metrics.statusBarVerticalMargin + (extraTitleHeightPx > 0 ? extraTitleHeightPx : 0) : 0;
  return text + (bars > 0 ? bars + metrics.progressBarMarginTop : 0);
}

int UITheme::getStatusBarV2BottomHeight(bool hasChapters, int extraTitleHeightPx) {
  if (!SETTINGS.sbEnabled) return 0;
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  const int barPx = statusBarThicknessPx(SETTINGS.sbBarThickness);
  int bars = 0;
  if (SETTINGS.sbBookBar == CrossPointSettings::SB_EDGE_BOTTOM) bars += barPx;
  if (SETTINGS.sbChapterBar == CrossPointSettings::SB_EDGE_BOTTOM && hasChapters) bars += barPx;
  const bool hasText = sbBandHasText(false, hasChapters);
  const int text = hasText ? metrics.statusBarVerticalMargin + (extraTitleHeightPx > 0 ? extraTitleHeightPx : 0) : 0;
  return text + (bars > 0 ? bars + metrics.progressBarMarginTop : 0);
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
  if (!SETTINGS.sbEnabled || SETTINGS.sbTitlePos == CrossPointSettings::SB_ANCHOR_OFF) return 1;
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
