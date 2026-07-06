#include "UITheme.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <Logging.h>

#include <memory>

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/StatusBar.h"
#include "components/themes/BaseTheme.h"
#include "components/themes/lyra/Lyra3CoversTheme.h"
#include "components/themes/lyra/LyraTheme.h"
#include "components/themes/roundedraff/RoundedRaffTheme.h"
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

void UITheme::setTheme(CrossPointSettings::UI_THEME type) {
  switch (type) {
    case CrossPointSettings::UI_THEME::CLASSIC:
      LOG_DBG("UI", "Using Classic theme");
      currentTheme = std::make_unique<BaseTheme>();
      currentMetrics = &BaseMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::LYRA:
      LOG_DBG("UI", "Using Lyra theme");
      currentTheme = std::make_unique<LyraTheme>();
      currentMetrics = &LyraMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::ROUNDEDRAFF:
      LOG_DBG("UI", "Using RoundedRaff theme");
      currentTheme = std::make_unique<RoundedRaffTheme>();
      currentMetrics = &RoundedRaffMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::LYRA_3_COVERS:
      LOG_DBG("UI", "Using Lyra 3 Covers theme");
      currentTheme = std::make_unique<Lyra3CoversTheme>();
      currentMetrics = &Lyra3CoversMetrics::values;
      break;
  }
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

int UITheme::getStatusBarHeight() {
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();

  // Add status bar margin
  const bool showStatusBar =
      SETTINGS.statusBarChapterPageCount || SETTINGS.statusBarBookProgressPercentage ||
      SETTINGS.statusBarTitle != CrossPointSettings::STATUS_BAR_TITLE::HIDE_TITLE || SETTINGS.statusBarBattery ||
      SETTINGS.statusBarClock != CrossPointSettings::STATUS_BAR_CLOCK_MODE::STATUS_BAR_CLOCK_HIDE;
  const bool showProgressBar =
      SETTINGS.statusBarProgressBar != CrossPointSettings::STATUS_BAR_PROGRESS_BAR::HIDE_PROGRESS;
  return (showStatusBar ? (metrics.statusBarVerticalMargin) : 0) +
         (showProgressBar ? (((SETTINGS.statusBarProgressBarThickness + 1) * 2) + metrics.progressBarMarginTop) : 0);
}

int UITheme::getProgressBarHeight() {
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  const bool showProgressBar =
      SETTINGS.statusBarProgressBar != CrossPointSettings::STATUS_BAR_PROGRESS_BAR::HIDE_PROGRESS;
  return (showProgressBar ? (((SETTINGS.statusBarProgressBarThickness + 1) * 2) + metrics.progressBarMarginTop) : 0);
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
      {SETTINGS.sbBatteryPos, false, true},
      {SETTINGS.sbClockPos, false, clockAvailable},
      {SETTINGS.sbTitlePos, SETTINGS.sbTitleSource == CrossPointSettings::SB_TITLE_CHAPTER, true},
      {SETTINGS.sbPagePos, true, true},
      {SETTINGS.sbBookPctPos, false, true},
      {SETTINGS.sbChapterPctPos, true, true},
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
