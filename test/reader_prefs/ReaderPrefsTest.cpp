// Host tests for the per-book ReaderPrefs sidecar: serialize/deserialize
// round-trip (HalFile + std::stringstream), version rejection, truncation
// safety, and the pure layout helpers (line compression + first-line indent).
//
// fromGlobal() and readerFontId() are NOT tested here — they depend on the
// CrossPointSettings singleton, which is not host-buildable. They are thin field
// copies / a resolver passthrough with no branching worth a device-only harness.

#include <gtest/gtest.h>

#include <sstream>

#include "ReaderPrefs.h"

namespace {

ReaderPrefs makeSample() {
  ReaderPrefs p;
  p.fontFamily = 0;
  p.fontSize = 4;
  p.lineSpacingPercent = 120;
  p.paragraphAlignment = 2;
  p.wordSpacing = 7;
  p.paragraphSpacing = 40;
  p.extraParagraphSpacing = 0;
  p.uniformMargins = 0;
  p.screenMargin = 12;
  p.screenMarginTop = 8;
  p.screenMarginBottom = 20;
  p.firstLineIndentMode = 1;
  p.firstLineIndentPercent = 50;
  p.hyphenationEnabled = 1;
  p.embeddedStyle = 1;
  p.focusReadingEnabled = 1;
  p.guideDotsEnabled = 1;
  p.imageRendering = 2;
  p.orientation = 3;
  std::strncpy(p.sdFontFamilyName, "MyBookFont", sizeof(p.sdFontFamilyName) - 1);
  return p;
}

void expectEqual(const ReaderPrefs& a, const ReaderPrefs& b) {
  EXPECT_EQ(a.fontFamily, b.fontFamily);
  EXPECT_EQ(a.fontSize, b.fontSize);
  EXPECT_EQ(a.lineSpacingPercent, b.lineSpacingPercent);
  EXPECT_EQ(a.paragraphAlignment, b.paragraphAlignment);
  EXPECT_EQ(a.wordSpacing, b.wordSpacing);
  EXPECT_EQ(a.paragraphSpacing, b.paragraphSpacing);
  EXPECT_EQ(a.extraParagraphSpacing, b.extraParagraphSpacing);
  EXPECT_EQ(a.uniformMargins, b.uniformMargins);
  EXPECT_EQ(a.screenMargin, b.screenMargin);
  EXPECT_EQ(a.screenMarginTop, b.screenMarginTop);
  EXPECT_EQ(a.screenMarginBottom, b.screenMarginBottom);
  EXPECT_EQ(a.firstLineIndentMode, b.firstLineIndentMode);
  EXPECT_EQ(a.firstLineIndentPercent, b.firstLineIndentPercent);
  EXPECT_EQ(a.hyphenationEnabled, b.hyphenationEnabled);
  EXPECT_EQ(a.embeddedStyle, b.embeddedStyle);
  EXPECT_EQ(a.focusReadingEnabled, b.focusReadingEnabled);
  EXPECT_EQ(a.guideDotsEnabled, b.guideDotsEnabled);
  EXPECT_EQ(a.imageRendering, b.imageRendering);
  EXPECT_EQ(a.orientation, b.orientation);
  EXPECT_STREQ(a.sdFontFamilyName, b.sdFontFamilyName);
}

// HalFile (device) path: write then read back is identical.
TEST(ReaderPrefs, HalFileRoundTrip) {
  const ReaderPrefs in = makeSample();
  HalFile f;
  ASSERT_TRUE(writeReaderPrefs(f, in));
  f.seek(0);
  ReaderPrefs out;
  ASSERT_TRUE(readReaderPrefs(f, out));
  expectEqual(in, out);
}

// std::stringstream (host) path exercises the templated overloads.
TEST(ReaderPrefs, StreamRoundTrip) {
  const ReaderPrefs in = makeSample();
  std::stringstream ss;
  writeReaderPrefs(ss, in);
  ReaderPrefs out;
  ASSERT_TRUE(readReaderPrefs(ss, out));
  expectEqual(in, out);
}

// A wrong version byte is rejected (caller then falls back to global settings).
TEST(ReaderPrefs, WrongVersionRejected) {
  const ReaderPrefs in = makeSample();
  HalFile f;
  const uint8_t bad = ReaderPrefs::VERSION + 1;  // future/unknown version
  f.write(&bad, 1);
  f.write(reinterpret_cast<const uint8_t*>(&in), sizeof(in));  // full payload present
  f.seek(0);
  ReaderPrefs out;
  EXPECT_FALSE(readReaderPrefs(f, out));
}

// A truncated sidecar (version byte only, payload missing) fails cleanly on the
// checked HalFile path rather than yielding a half-filled struct.
TEST(ReaderPrefs, TruncatedRejected) {
  HalFile f;
  const uint8_t ver = ReaderPrefs::VERSION;
  f.write(&ver, 1);  // version present, no payload
  f.seek(0);
  ReaderPrefs out;
  EXPECT_FALSE(readReaderPrefs(f, out));
}

// Empty file (no bytes) fails cleanly.
TEST(ReaderPrefs, EmptyRejected) {
  HalFile f;
  ReaderPrefs out;
  EXPECT_FALSE(readReaderPrefs(f, out));
}

// Line compression mirrors CrossPointSettings::getReaderLineCompression, clamped
// to 35..150 percent.
TEST(ReaderPrefs, LineCompression) {
  ReaderPrefs p;
  p.lineSpacingPercent = 100;
  EXPECT_FLOAT_EQ(readerLineCompression(p), 1.0f);
  p.lineSpacingPercent = 150;
  EXPECT_FLOAT_EQ(readerLineCompression(p), 1.5f);
  p.lineSpacingPercent = 10;  // below min -> clamp to 35
  EXPECT_FLOAT_EQ(readerLineCompression(p), 0.35f);
  p.lineSpacingPercent = 250;  // above max -> clamp to 150
  EXPECT_FLOAT_EQ(readerLineCompression(p), 1.5f);
}

// First-line indent: -1 (use book/CSS) in BOOK mode; percent mapped onto
// 0..viewportWidth/2 in PERCENT mode.
TEST(ReaderPrefs, FirstLineIndent) {
  ReaderPrefs p;
  p.firstLineIndentMode = 0;  // BOOK
  p.firstLineIndentPercent = 50;
  EXPECT_EQ(readerFirstLineIndentPx(p, 600), -1);
  p.firstLineIndentMode = 1;  // PERCENT
  p.firstLineIndentPercent = 0;
  EXPECT_EQ(readerFirstLineIndentPx(p, 600), 0);
  p.firstLineIndentPercent = 100;  // 100% == half the column
  EXPECT_EQ(readerFirstLineIndentPx(p, 600), 300);
  p.firstLineIndentPercent = 50;
  EXPECT_EQ(readerFirstLineIndentPx(p, 600), 150);
}

}  // namespace
