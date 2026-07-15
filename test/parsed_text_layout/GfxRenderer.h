#pragma once

// Host-test stub for the firmware GfxRenderer. The real class drives the
// e-ink panel and font caches; layout code (ParsedText/TextBlock) only needs
// deterministic text METRICS plus no-op draw calls, so behavior goldens can be
// asserted on the host. Metrics model: every codepoint advances kCpAdvance px,
// space is kSpaceWidth px, kerning is 0. Deliberately simple — the tests
// exercise line-breaking/justification logic, not font rendering.

#include <BidiUtils.h>
#include <EpdFontFamily.h>

#include <cstdint>
#include <string>
#include <vector>

// The real GfxRenderer.h declares this inside BidiUtils (not BidiUtils.h);
// mirror it so call sites compile identically against the stub.
namespace BidiUtils {
enum class BidiBaseDir : signed char { AUTO = -1, LTR = 0, RTL = 1 };
}  // namespace BidiUtils

class GfxRenderer {
 public:
  static constexpr int kCpAdvance = 10;
  static constexpr int kSpaceWidth = 5;
  static constexpr int kAscender = 16;

  static int countCodepoints(const char* text) {
    int n = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
      if ((*p & 0xC0) != 0x80) ++n;  // count non-continuation bytes
    }
    return n;
  }

  bool isFontCacheScanning() const { return false; }
  bool isSdCardFont(int /*fontId*/) const { return false; }
  void ensureSdCardFontReady(int /*fontId*/, const char* /*utf8Text*/, uint8_t /*styleMask*/ = 0x0F) const {}
  void ensureSdCardFontReady(int /*fontId*/, const std::vector<std::string>& /*words*/, bool /*includeHyphen*/,
                             uint8_t /*styleMask*/ = 0x0F) const {}

  int getTextAdvanceX(int /*fontId*/, const char* text, EpdFontFamily::Style /*style*/) const {
    return countCodepoints(text) * kCpAdvance;
  }
  int getTextWidth(int /*fontId*/, const char* text, EpdFontFamily::Style /*style*/ = EpdFontFamily::REGULAR,
                   BidiUtils::BidiBaseDir /*baseDir*/ = BidiUtils::BidiBaseDir::AUTO) const {
    return countCodepoints(text) * kCpAdvance;
  }
  int getSpaceWidth(int /*fontId*/, EpdFontFamily::Style /*style*/ = EpdFontFamily::REGULAR) const {
    return kSpaceWidth;
  }
  int getSpaceAdvance(int /*fontId*/, uint32_t /*leftCp*/, uint32_t /*rightCp*/, EpdFontFamily::Style /*style*/) const {
    return kSpaceWidth;
  }
  int getKerning(int /*fontId*/, uint32_t /*leftCp*/, uint32_t /*rightCp*/, EpdFontFamily::Style /*style*/) const {
    return 0;
  }
  int getFontAscenderSize(int /*fontId*/) const { return kAscender; }

  void drawLine(int, int, int, int, bool = true) const {}
  void drawLine(int, int, int, int, int, bool) const {}
  void drawText(int, int, int, const char*, bool = true, EpdFontFamily::Style = EpdFontFamily::REGULAR,
                BidiUtils::BidiBaseDir = BidiUtils::BidiBaseDir::AUTO) const {}
};
