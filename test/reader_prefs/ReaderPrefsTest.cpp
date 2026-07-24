// Host tests for the per-book ReaderPrefs snapshot: the [version][POD] stream
// serialization and its guards. fromGlobal() and the HalFile overloads depend on
// the CrossPointSettings singleton / SD stack (not host-buildable), so they are
// covered on-device, not here — this file locks down the format logic.
#include <gtest/gtest.h>

#include <cstring>
#include <sstream>

#include "ReaderPrefs.h"

namespace {

// A fully non-default snapshot so a round-trip that drops or reorders any field fails.
ReaderPrefs makeSample() {
  ReaderPrefs p;
  p.fontFamily = 1;
  p.fontSize = 3;
  p.lineSpacingPercent = 120;
  p.paragraphAlignment = 4;
  p.extraParagraphSpacing = 0;
  p.paragraphSpacing = 80;
  p.screenMargin = 35;
  p.screenMarginTop = 12;
  p.screenMarginBottom = 18;
  p.uniformMargins = 0;
  p.dynamicMargins = 2;
  p.focusReadingEnabled = 1;
  p.guideDotsEnabled = 1;
  p.hyphenationEnabled = 1;
  p.embeddedStyle = 0;
  p.textAntiAliasing = 0;
  p.imageRendering = 2;
  p.paragraphNumbering = 2;  // whole book
  p.paperbackLookBody = 0;
  p.paperbackLookStatus = 1;
  p.firstLineIndentMode = 1;  // Custom %
  p.firstLineIndentPercent = 40;
  std::memset(p.sdFontFamilyName, 0, sizeof(p.sdFontFamilyName));
  std::strncpy(p.sdFontFamilyName, "Bookerly", sizeof(p.sdFontFamilyName) - 1);
  return p;
}

void expectEqual(const ReaderPrefs& a, const ReaderPrefs& b) {
  EXPECT_EQ(a.fontFamily, b.fontFamily);
  EXPECT_EQ(a.fontSize, b.fontSize);
  EXPECT_EQ(a.lineSpacingPercent, b.lineSpacingPercent);
  EXPECT_EQ(a.paragraphAlignment, b.paragraphAlignment);
  EXPECT_EQ(a.extraParagraphSpacing, b.extraParagraphSpacing);
  EXPECT_EQ(a.paragraphSpacing, b.paragraphSpacing);
  EXPECT_EQ(a.screenMargin, b.screenMargin);
  EXPECT_EQ(a.screenMarginTop, b.screenMarginTop);
  EXPECT_EQ(a.screenMarginBottom, b.screenMarginBottom);
  EXPECT_EQ(a.uniformMargins, b.uniformMargins);
  EXPECT_EQ(a.dynamicMargins, b.dynamicMargins);
  EXPECT_EQ(a.focusReadingEnabled, b.focusReadingEnabled);
  EXPECT_EQ(a.guideDotsEnabled, b.guideDotsEnabled);
  EXPECT_EQ(a.hyphenationEnabled, b.hyphenationEnabled);
  EXPECT_EQ(a.embeddedStyle, b.embeddedStyle);
  EXPECT_EQ(a.textAntiAliasing, b.textAntiAliasing);
  EXPECT_EQ(a.imageRendering, b.imageRendering);
  EXPECT_EQ(a.paragraphNumbering, b.paragraphNumbering);
  EXPECT_EQ(a.paperbackLookBody, b.paperbackLookBody);
  EXPECT_EQ(a.paperbackLookStatus, b.paperbackLookStatus);
  EXPECT_EQ(a.firstLineIndentMode, b.firstLineIndentMode);
  EXPECT_EQ(a.firstLineIndentPercent, b.firstLineIndentPercent);
  EXPECT_STREQ(a.sdFontFamilyName, b.sdFontFamilyName);
  // POD change-detection is a whole-blob memcmp, so the bytes must match exactly.
  EXPECT_EQ(0, std::memcmp(&a, &b, sizeof(ReaderPrefs)));
}

}  // namespace

TEST(ReaderPrefs, ParagraphNumberingDefaultsOff) {
  ReaderPrefs p;
  EXPECT_EQ(0, p.paragraphNumbering);
}

TEST(ReaderPrefs, FirstLineIndentDefaultsToBook) {
  ReaderPrefs p;
  EXPECT_EQ(0, p.firstLineIndentMode);  // Book (respect CSS)
  EXPECT_EQ(0, p.firstLineIndentPercent);
}

TEST(ReaderPrefs, StreamRoundTrip) {
  const ReaderPrefs original = makeSample();
  std::stringstream ss;
  writeReaderPrefs(ss, original);

  ReaderPrefs loaded;
  ASSERT_TRUE(readReaderPrefs(ss, loaded));
  expectEqual(original, loaded);
}

TEST(ReaderPrefs, WrongVersionRejected) {
  const ReaderPrefs sample = makeSample();
  std::stringstream ss;
  const uint8_t badVersion = ReaderPrefs::VERSION + 1;
  ss.write(reinterpret_cast<const char*>(&badVersion), 1);
  ss.write(reinterpret_cast<const char*>(&sample), sizeof(ReaderPrefs));

  ReaderPrefs loaded;
  EXPECT_FALSE(readReaderPrefs(ss, loaded));
}

TEST(ReaderPrefs, TruncatedRejected) {
  // Version byte present, POD payload missing entirely.
  std::stringstream ss;
  const uint8_t version = ReaderPrefs::VERSION;
  ss.write(reinterpret_cast<const char*>(&version), 1);

  ReaderPrefs loaded;
  EXPECT_FALSE(readReaderPrefs(ss, loaded));
}

TEST(ReaderPrefs, EmptyRejected) {
  std::stringstream ss;
  ReaderPrefs loaded;
  EXPECT_FALSE(readReaderPrefs(ss, loaded));
}
