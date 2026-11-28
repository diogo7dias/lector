#pragma once
// Layout-only renderer for the chapter fuzzer: fixed non-zero metrics (a zero line height
// or advance would loop forever where the device cannot), drawing is a no-op.
#include <BidiUtils.h>
#include <EpdFontFamily.h>
#include <Utf8.h>

#include <cstdint>
#include <deque>
#include <string>

class FontCacheManager;

enum Color : uint8_t { Clear = 0x00, White = 0x01, LightGray = 0x05, DarkGray = 0x0A, Black = 0x10 };

class GfxRenderer {
 public:
  using Dir = BidiUtils::BidiBaseDir;
  using Style = EpdFontFamily::Style;

  // Font ids 1..8 exist; the scaled variants of the body font resolve to id + 1 / id - 1.
  int resolveFontVariant(const int fontId, const float scale) const {
    if (scale > 1.2f) return fontId + 1;
    if (scale < 0.9f && fontId > 1) return fontId - 1;
    return fontId;
  }
  bool ensureFontLoaded(const int fontId) const { return fontId >= 1 && fontId <= 8; }
  bool isSdCardFont(int) const { return false; }
  void ensureSdCardFontReady(int, const char*, uint8_t = 0x0F) const {}
  void ensureSdCardFontReady(int, const std::deque<std::string>&, uint8_t = 0x0F, bool = false) const {}
  FontCacheManager* getFontCacheManager() const { return nullptr; }
  bool isFontCacheScanning() const { return false; }
  int getScreenWidth() const { return 480; }
  int getScreenHeight() const { return 800; }

  static int size(const int fontId) { return 6 + 2 * (fontId < 1 ? 1 : fontId > 8 ? 8 : fontId); }
  int getTextAdvanceX(const int fontId, const char* text, Style style) const {
    int width = 0;
    const auto* p = reinterpret_cast<const unsigned char*>(text);
    while (*p) {
      const uint32_t cp = utf8NextCodepoint(&p);
      width += cp == ' ' ? getSpaceWidth(fontId, style) : cp < 0x300 ? size(fontId) : 2 * size(fontId);
    }
    return width;
  }
  int getTextWidth(const int fontId, const char* text, Style style = EpdFontFamily::REGULAR, Dir = Dir::AUTO) const {
    return getTextAdvanceX(fontId, text, style);
  }
  int spacedGlyphCount(int, const char* text, Style, Dir = Dir::AUTO) const {
    int count = 0;
    const auto* p = reinterpret_cast<const unsigned char*>(text);
    while (*p) count += utf8NextCodepoint(&p) != 0;
    return count;
  }
  int getSpaceWidth(const int fontId, Style = EpdFontFamily::REGULAR) const { return size(fontId) / 2; }
  int getKerning(int, uint32_t left, uint32_t right, Style) const { return left == 'A' && right == 'V' ? -1 : 0; }
  int getSpaceAdvance(const int fontId, uint32_t left, uint32_t right, Style style) const {
    return getSpaceWidth(fontId, style) + getKerning(fontId, left, ' ', style) + getKerning(fontId, ' ', right, style);
  }
  int getFontAscenderSize(const int fontId) const { return size(fontId); }
  int getLineHeight(const int fontId) const { return size(fontId) + size(fontId) / 2; }
  int getLineHeight(const int fontId, const float compression) const {
    const int h = static_cast<int>(getLineHeight(fontId) * compression);
    return h > 0 ? h : 1;
  }
  bool getCodepointMetrics(const int fontId, const uint32_t cp, Style, int32_t& advanceFP, int& top) const {
    if (cp == 0) return false;
    advanceFP = size(fontId) << 4;
    top = size(fontId);
    return true;
  }

  void drawText(int, int, int, const char*, bool = true, Style = EpdFontFamily::REGULAR, Dir = Dir::AUTO,
                int = 0) const {}
  int drawScaledCodepoint(const int fontId, uint32_t, Style, int, int, const int scale256) const {
    return size(fontId) * scale256 / 256;
  }
  void drawLine(int, int, int, int, bool = true) const {}
  void drawLine(int, int, int, int, int, bool) const {}
  void fillRect(int, int, int, int, bool = true) const {}
  void fillRectDither(int, int, int, int, Color) const {}
  void preserveImagePolarity(int, int, int, int) const {}
  bool glyphIntersectsStrip(int, int, int, int) const { return true; }

  class FrameBufferLoan {
   public:
    explicit FrameBufferLoan(GfxRenderer&) {}
    void end() {}
  };
};
