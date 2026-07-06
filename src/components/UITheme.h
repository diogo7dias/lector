#pragma once

#include <EpdFontFamily.h>

#include <functional>
#include <memory>

#include "CrossPointSettings.h"
#include "components/themes/BaseTheme.h"

class UITheme {
  // Static instance
  static UITheme instance;

 public:
  UITheme();
  static UITheme& getInstance() { return instance; }

  const ThemeMetrics& getMetrics() const { return *currentMetrics; }
  const BaseTheme& getTheme() const { return *currentTheme; }
  Rect getScreenSafeArea(const GfxRenderer& renderer, bool hasFrontButtonHints = false,
                         bool hasSideButtonHints = false);
  static void drawCenteredText(const GfxRenderer& renderer, Rect screen, int fontId, int y, const char* text,
                               bool black = true, EpdFontFamily::Style style = EpdFontFamily::REGULAR);
  void reload();
  void setTheme(CrossPointSettings::UI_THEME type);
  static int getNumberOfItemsPerPage(const GfxRenderer& renderer, bool hasHeader, bool hasTabBar, bool hasButtonHints,
                                     bool hasSubtitle, int extraReservedHeight = 0);
  static std::string getCoverThumbPath(std::string coverBmpPath, int coverHeight);
  static UIIcon getFileIcon(const std::string& filename);
  static int getStatusBarHeight();
  static int getProgressBarHeight();
  // v2 status bar: pixels to reserve at the top / bottom edge for the text band(s)
  // plus any progress bars on that edge. hasChapters filters chapter-only items,
  // which don't render on chapterless books (TXT, flat XTC). `extraTitleHeightPx`
  // is the extra height a wrapped (truncate-off) title needs beyond its first
  // line, added only when that band actually holds the title.
  static int getStatusBarV2TopHeight(bool hasChapters, int extraTitleHeightPx = 0);
  static int getStatusBarV2BottomHeight(bool hasChapters, int extraTitleHeightPx = 0);
  // Usable text-band width the v2 bar wraps/aligns within (screen minus the
  // status-bar horizontal margins and the oriented viewable insets). Shared by the
  // renderer and the reader's wrap-line count so both agree.
  static int getStatusBarV2BandWidth(const GfxRenderer& renderer);
  // Number of lines the title wraps to (>= 1). Returns 1 unless the title is
  // placed, non-empty, and "Truncate Title" is OFF (greedy), in which case it is
  // the wrapped line count within the band width, capped for safety.
  static int getStatusBarV2TitleLines(const GfxRenderer& renderer, const char* title);

 private:
  const ThemeMetrics* currentMetrics;
  std::unique_ptr<BaseTheme> currentTheme;
};

// Helper macro to access current theme
#define GUI UITheme::getInstance().getTheme()
