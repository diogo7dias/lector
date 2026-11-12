#pragma once

#include <EpdFontFamily.h>

#include <functional>

#include "CrossPointSettings.h"
#include "components/themes/BaseTheme.h"

class UITheme {
  // Static instance
  static UITheme instance;

 public:
  enum class TextVerticalAlignment { TOP, CENTER, BOTTOM };

  UITheme();
  static UITheme& getInstance() { return instance; }

  const ThemeMetrics& getMetrics() const;
  const BaseTheme& getTheme() const { return theme; }

  // A popup or busy banner was painted on top of the screen (a message strip or an
  // option list). The panel keeps that band after the popup goes, so the next
  // full-screen paint of the screen underneath owes a clean pass: the list, grid
  // and status bases promote that single paint from FAST, then consume this. Only
  // the first paint after the popup leaves, never every later one.
  static void noteOverlayPainted() { overlayPainted = true; }
  static bool consumeOverlayPainted() {
    const bool painted = overlayPainted;
    overlayPainted = false;
    return painted;
  }
  Rect getScreenSafeArea(const GfxRenderer& renderer, bool hasFrontButtonHints = false,
                         bool hasSideButtonHints = false);
  static void drawCenteredText(const GfxRenderer& renderer, Rect screen, int fontId, int y, const char* text,
                               bool black = true, EpdFontFamily::Style style = EpdFontFamily::REGULAR);
  static UIIcon getFileIcon(const std::string& filename);
  // v2 status bar: pixels to reserve at the top / bottom edge for the text band(s)
  // plus any progress bars on that edge. hasChapters filters chapter-only items,
  // which don't render on chapterless books (TXT, flat XTC). `extraTitleHeightPx`
  // is the extra height a wrapped (truncate-off) title needs beyond its first
  // line, added only when that band actually holds the title.
  static int getStatusBarV2TopHeight(const StatusBarBlock& sb, bool hasChapters, int extraTitleHeightPx = 0);
  static int getStatusBarV2BottomHeight(const StatusBarBlock& sb, bool hasChapters, int extraTitleHeightPx = 0);
  // Usable text-band width the v2 bar wraps/aligns within (screen minus the
  // status-bar horizontal margins and the oriented viewable insets). Shared by the
  // renderer and the reader's wrap-line count so both agree.
  static int getStatusBarV2BandWidth(const GfxRenderer& renderer);
  // Number of lines the title slot takes (>= 1), and in `onTop` whether that is the
  // top band. A lone title takes 1 unless "Truncate Title" is OFF (greedy), in which
  // case it is the wrapped line count within the band width, capped for safety. A
  // stack (book title above chapter title) adds the book title's lines on top. Takes
  // the same strings the bar draws, so the reserved height and the draw agree.
  static int getStatusBarV2TitleLines(const StatusBarBlock& sb, const GfxRenderer& renderer, bool hasChapters,
                                      const char* chapterTitle, const char* bookTitle, bool* onTop);
  // Safety ceiling on one title's wrapped lines: real book/chapter titles never
  // approach it, but it bounds the reserved band (and the render loop).
  static constexpr int kMaxStatusBarTitleLines = 6;

 private:
  // Written and read only under the render lock, like the framebuffer it describes.
  static bool overlayPainted;
  const ThemeMetrics* currentMetrics;
  BaseTheme theme;
  mutable ThemeMetrics adjustedMetrics;
  mutable bool metricsValid = false;
  mutable bool metricsForTouch = false;
};

// Helper macro to access current theme
#define GUI UITheme::getInstance().getTheme()
