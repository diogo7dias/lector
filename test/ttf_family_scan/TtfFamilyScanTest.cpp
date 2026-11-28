#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "EpdFontFamily.h"
#include "TtfFamilyScan.h"

using namespace ttfscan;

// --- style inference ---------------------------------------------------------------

struct StyleCase {
  const char* name;
  bool tokened;
  uint8_t style;
};

TEST(TtfStyleInference, FileNameTable) {
  const StyleCase cases[] = {
      {"Foo-Regular.ttf", true, STYLE_REGULAR},
      {"Foo-Bold.ttf", true, STYLE_BOLD},
      {"Foo-Italic.otf", true, STYLE_ITALIC},
      {"Foo-BoldItalic.ttf", true, STYLE_BOLD_ITALIC},
      {"Foo-Bold-Italic.ttf", true, STYLE_BOLD_ITALIC},
      {"Foo_bold_oblique.ttf", true, STYLE_BOLD_ITALIC},
      {"Foo-BoldOblique.ttf", true, STYLE_BOLD_ITALIC},
      {"Foo-Ital.ttf", true, STYLE_ITALIC},
      {"FOO-BOLD.TTF", true, STYLE_BOLD},
      {"Foo-Book.ttf", true, STYLE_REGULAR},
      {"Foo-Roman.ttf", true, STYLE_REGULAR},
      {"Foo-Text.otf", true, STYLE_REGULAR},
      {"Foo-Normal.ttf", true, STYLE_REGULAR},
      {"Foo-SemiBold.ttf", true, STYLE_BOLD},
      {"Foo-DemiBold.ttf", true, STYLE_BOLD},
      {"Foo-Medium.ttf", true, STYLE_BOLD},
      {"Foo-ExtraBold.ttf", true, STYLE_BOLD},
      {"Foo-Light.ttf", true, STYLE_REGULAR},
      {"Foo-Thin.ttf", true, STYLE_REGULAR},
      {"Foo-LightItalic.ttf", true, STYLE_ITALIC},
      {"Foo-SemiBoldItalic.ttf", true, STYLE_BOLD_ITALIC},
      {"Foo-MediumItalic.otf", true, STYLE_BOLD_ITALIC},
      {"Foo-ExtraBoldItalic.ttf", true, STYLE_BOLD_ITALIC},
      {"Foo-Italic-Display.ttf", true, STYLE_ITALIC},
      {"Digital.ttf", false, 0},  // "ital" only counts as a word
      {"Kobold.ttf", false, 0},   // "bold" only counts as a word
      {"Hospitalic.ttf", false, 0},
      {"Foo.ttf", false, 0},
      {"FooBold.ttf", false, 0},  // no word boundary: not a token
      {"Bookerly Display.ttf", false, 0},
  };
  for (const auto& c : cases) {
    uint8_t style = 99;
    const bool tokened = inferTtfStyle(c.name, style);
    EXPECT_EQ(tokened, c.tokened) << c.name;
    if (c.tokened) {
      EXPECT_EQ(style, c.style) << c.name;
    }
  }
}

TEST(TtfStyleInference, WordBoundary) {
  EXPECT_TRUE(hasWord("foo-bold", "bold"));
  EXPECT_TRUE(hasWord("bold", "bold"));
  EXPECT_TRUE(hasWord("foo bold italic", "bold"));
  EXPECT_FALSE(hasWord("foo-semibold", "bold"));
  EXPECT_FALSE(hasWord("foobold", "bold"));
  EXPECT_FALSE(hasWord("foo-boldx", "bold"));
}

// --- file acceptance ---------------------------------------------------------------

TEST(TtfFileFilter, AcceptsOnlyVectorFonts) {
  EXPECT_TRUE(isTtfFontFile("Foo.ttf"));
  EXPECT_TRUE(isTtfFontFile("Foo.OTF"));
  EXPECT_TRUE(isTtfFontFile("Foo.TTF"));
  EXPECT_FALSE(isTtfFontFile("Foo.cpfont"));
  EXPECT_FALSE(isTtfFontFile("Foo.ttf.tmp"));
  EXPECT_FALSE(isTtfFontFile("Foo.ttf~"));
  EXPECT_FALSE(isTtfFontFile("._Foo.ttf"));
  EXPECT_FALSE(isTtfFontFile(".hidden.ttf"));
  EXPECT_FALSE(isTtfFontFile("_Foo.ttf"));
  EXPECT_FALSE(isTtfFontFile("free-fonts.json"));
  EXPECT_FALSE(isTtfFontFile(""));
  EXPECT_FALSE(isTtfFontFile(nullptr));
}

// --- family slot resolution -----------------------------------------------------------

namespace {
TtfFamilyFaces resolve(const std::vector<const char*>& names) {
  TtfFamilyFaces f;
  for (const char* n : names) f.offer(n);
  f.finish();
  return f;
}
}  // namespace

TEST(TtfFamilyFaces, FourCanonicalFaces) {
  const auto f = resolve({"Foo-Bold.ttf", "Foo-Regular.ttf", "Foo-BoldItalic.ttf", "Foo-Italic.ttf"});
  EXPECT_STREQ(f.file(STYLE_REGULAR), "Foo-Regular.ttf");
  EXPECT_STREQ(f.file(STYLE_BOLD), "Foo-Bold.ttf");
  EXPECT_STREQ(f.file(STYLE_ITALIC), "Foo-Italic.ttf");
  EXPECT_STREQ(f.file(STYLE_BOLD_ITALIC), "Foo-BoldItalic.ttf");
  EXPECT_FALSE(f.empty());
}

TEST(TtfFamilyFaces, LoneStyledFileBecomesRegular) {
  const auto f = resolve({"Foo-Bold.ttf"});
  EXPECT_STREQ(f.file(STYLE_REGULAR), "Foo-Bold.ttf");
  EXPECT_FALSE(f.has(STYLE_BOLD));
}

TEST(TtfFamilyFaces, NonFontFilesAreIgnored) {
  TtfFamilyFaces f;
  EXPECT_FALSE(f.offer("Foo_14.cpfont"));
  EXPECT_FALSE(f.offer(".DS_Store"));
  EXPECT_FALSE(f.offer("Foo-Regular.ttf~"));
  EXPECT_TRUE(f.empty());
  EXPECT_TRUE(f.offer("Foo-Italic.ttf"));
  f.finish();
  EXPECT_STREQ(f.file(STYLE_REGULAR), "Foo-Italic.ttf");
  EXPECT_FALSE(f.has(STYLE_ITALIC));
}

TEST(TtfFamilyFaces, NoRegularPromotesFirstPresentSlot) {
  const auto f = resolve({"Foo-Italic.ttf", "Foo-Bold.ttf"});
  EXPECT_STREQ(f.file(STYLE_REGULAR), "Foo-Bold.ttf");
  EXPECT_FALSE(f.has(STYLE_BOLD));
  EXPECT_STREQ(f.file(STYLE_ITALIC), "Foo-Italic.ttf");

  const auto g = resolve({"Foo-BoldItalic.ttf", "Foo-Italic.ttf"});
  EXPECT_STREQ(g.file(STYLE_REGULAR), "Foo-Italic.ttf");
  EXPECT_FALSE(g.has(STYLE_ITALIC));
  EXPECT_STREQ(g.file(STYLE_BOLD_ITALIC), "Foo-BoldItalic.ttf");
}

TEST(TtfFamilyFaces, TokenlessFileIsTheRegularCandidate) {
  const auto f = resolve({"Bookerly-Bold.ttf", "Bookerly Display.ttf"});
  EXPECT_STREQ(f.file(STYLE_REGULAR), "Bookerly Display.ttf");
  EXPECT_STREQ(f.file(STYLE_BOLD), "Bookerly-Bold.ttf");
}

TEST(TtfFamilyFaces, ExplicitRegularBeatsTokenless) {
  const auto f = resolve({"Foo.ttf", "Foo-Regular.ttf"});
  EXPECT_STREQ(f.file(STYLE_REGULAR), "Foo-Regular.ttf");
}

TEST(TtfFamilyFaces, DuplicateStyleLexicographicallyFirstWins) {
  const auto f = resolve({"Zeta-Regular.ttf", "alpha-Regular.ttf", "Mid-Regular.ttf"});
  EXPECT_STREQ(f.file(STYLE_REGULAR), "alpha-Regular.ttf");
  const auto g = resolve({"b.ttf", "a.ttf", "x-Bold.ttf"});
  EXPECT_STREQ(g.file(STYLE_REGULAR), "a.ttf");
  EXPECT_STREQ(g.file(STYLE_BOLD), "x-Bold.ttf");
}

TEST(TtfFamilyFaces, ClearResets) {
  TtfFamilyFaces f;
  f.offer("Foo-Regular.ttf");
  f.finish();
  f.clear();
  EXPECT_TRUE(f.empty());
  EXPECT_FALSE(f.has(STYLE_REGULAR));
  EXPECT_STREQ(f.file(STYLE_REGULAR), "");
  EXPECT_STREQ(f.file(7), "");
}

// --- sizes ------------------------------------------------------------------------------

TEST(TtfSizes, ClampAndEmPixels) {
  EXPECT_EQ(clampTtfPointSize(5), TTF_MIN_PT);
  EXPECT_EQ(clampTtfPointSize(14), 14);
  EXPECT_EQ(clampTtfPointSize(99), TTF_MAX_PT);
  // 150 DPI, the .cpfont converter's default: 14 pt is a 29 px em.
  EXPECT_EQ(ttfEmPixels(14), 29);
  EXPECT_EQ(ttfEmPixels(8), 17);
  EXPECT_EQ(ttfEmPixels(40), 83);
}

// The family carries its point size as pixels at FONT_RASTER_DPI; the TTF loader sizes its em from
// the same constant, so a face and the family that holds it agree to within the loader's rounding.
TEST(TtfSizes, FamilyKnowsItsSizeInPixels) {
  EXPECT_EQ(FONT_RASTER_DPI, 150);
  for (uint8_t pt = TTF_MIN_PT; pt <= TTF_MAX_PT; ++pt) {
    const EpdFontFamily family(pt, nullptr);
    EXPECT_NEAR(family.getSizeInPixels(), pt * 150.0f / 72.0f, 1e-4f);
    EXPECT_LE(std::fabs(family.getSizeInPixels() - ttfEmPixels(pt)), 0.5f) << "pt " << int(pt);
  }
  EXPECT_FLOAT_EQ(EpdFontFamily(0, nullptr).getSizeInPixels(), 0.0f);
}

// --- cache fingerprint (font id) -------------------------------------------------------

TEST(TtfFontId, FamilySizeAndContentAllChangeTheId) {
  const int base = fontIdForFamilySize(0x12345678u, "Bookerly", 14);
  EXPECT_NE(base, 0);
  EXPECT_EQ(base, fontIdForFamilySize(0x12345678u, "Bookerly", 14));
  EXPECT_NE(base, fontIdForFamilySize(0x12345678u, "Bookerly", 15));
  EXPECT_NE(base, fontIdForFamilySize(0x12345678u, "Bookerlz", 14));
  EXPECT_NE(base, fontIdForFamilySize(0x12345679u, "Bookerly", 14));
}

TEST(TtfFontId, ContentHashFollowsBytes) {
  const uint8_t a[] = {1, 2, 3};
  const uint8_t b[] = {1, 2, 4};
  EXPECT_NE(fnv1a::hash32(a, sizeof(a)), fnv1a::hash32(b, sizeof(b)));
  EXPECT_EQ(fnv1a::hash32(a, sizeof(a)), fnv1a::hash32(a, sizeof(a)));
}

// --- sfnt metrics -------------------------------------------------------------------------

namespace {
void put16(std::vector<uint8_t>& v, size_t at, uint16_t x) {
  v[at] = static_cast<uint8_t>(x >> 8);
  v[at + 1] = static_cast<uint8_t>(x);
}
void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
  v[at] = static_cast<uint8_t>(x >> 24);
  v[at + 1] = static_cast<uint8_t>(x >> 16);
  v[at + 2] = static_cast<uint8_t>(x >> 8);
  v[at + 3] = static_cast<uint8_t>(x);
}

// Minimal sfnt: directory with 'head' and 'hhea' only.
std::vector<uint8_t> makeFont(uint16_t upem, int16_t asc, int16_t desc, int16_t gap, uint32_t tag = 0x00010000u) {
  const size_t headOff = 12 + 2 * 16;
  const size_t hheaOff = headOff + 54;
  std::vector<uint8_t> v(hheaOff + 36, 0);
  put32(v, 0, tag);
  put16(v, 4, 2);
  memcpy(&v[12], "head", 4);
  put32(v, 12 + 8, static_cast<uint32_t>(headOff));
  put32(v, 12 + 12, 54);
  memcpy(&v[28], "hhea", 4);
  put32(v, 28 + 8, static_cast<uint32_t>(hheaOff));
  put32(v, 28 + 12, 36);
  put16(v, headOff + 18, upem);
  put16(v, hheaOff + 4, static_cast<uint16_t>(asc));
  put16(v, hheaOff + 6, static_cast<uint16_t>(desc));
  put16(v, hheaOff + 8, static_cast<uint16_t>(gap));
  return v;
}
}  // namespace

TEST(TtfMetricsParse, ReadsHeadAndHhea) {
  const auto v = makeFont(1000, 800, -200, 50);
  TtfMetrics m;
  ASSERT_TRUE(readTtfMetrics(v.data(), v.size(), m));
  EXPECT_EQ(m.unitsPerEm, 1000);
  EXPECT_EQ(m.ascender, 800);
  EXPECT_EQ(m.descender, -200);
  EXPECT_EQ(m.lineGap, 50);
}

TEST(TtfMetricsParse, AcceptsOttoAndCollections) {
  const auto otf = makeFont(2048, 1638, -410, 0, 0x4F54544Fu);
  TtfMetrics m;
  EXPECT_TRUE(readTtfMetrics(otf.data(), otf.size(), m));
  EXPECT_EQ(m.unitsPerEm, 2048);

  // ttcf header (16 bytes) followed by the same font.
  std::vector<uint8_t> ttc(16, 0);
  memcpy(&ttc[0], "ttcf", 4);
  put32(ttc, 8, 1);
  put32(ttc, 12, 16);
  const auto inner = makeFont(1000, 700, -300, 0);
  ttc.insert(ttc.end(), inner.begin(), inner.end());
  // Table offsets inside the collection are absolute: patch them.
  put32(ttc, 16 + 12 + 8, 16 + 12 + 2 * 16);
  put32(ttc, 16 + 28 + 8, 16 + 12 + 2 * 16 + 54);
  ASSERT_TRUE(readTtfMetrics(ttc.data(), ttc.size(), m));
  EXPECT_EQ(m.ascender, 700);
}

TEST(TtfMetricsParse, RejectsForeignTruncatedAndOutOfBounds) {
  TtfMetrics m;
  EXPECT_FALSE(readTtfMetrics(nullptr, 100, m));
  const auto v = makeFont(1000, 800, -200, 0);
  EXPECT_FALSE(readTtfMetrics(v.data(), 11, m));
  EXPECT_FALSE(readTtfMetrics(v.data(), 40, m));  // directory cut short
  auto bad = v;
  put32(bad, 0, 0x12345678u);
  EXPECT_FALSE(readTtfMetrics(bad.data(), bad.size(), m));
  auto oob = v;
  put32(oob, 12 + 8, 100000);  // head offset beyond the file: table ignored, so missing
  EXPECT_FALSE(readTtfMetrics(oob.data(), oob.size(), m));
  auto inverted = makeFont(1000, -200, 800, 0);  // ascender below descender
  EXPECT_FALSE(readTtfMetrics(inverted.data(), inverted.size(), m));
  auto zeroUpem = makeFont(0, 800, -200, 0);
  EXPECT_FALSE(readTtfMetrics(zeroUpem.data(), zeroUpem.size(), m));
}

TEST(TtfMetricsScale, HeightForEmAndUnitScaling) {
  TtfMetrics m;
  m.unitsPerEm = 1000;
  m.ascender = 800;
  m.descender = -200;
  m.lineGap = 0;
  EXPECT_EQ(ttfHeightForEm(m, 29), 29);  // extent == em
  m.ascender = 1000;
  m.descender = -250;
  EXPECT_EQ(ttfHeightForEm(m, 29), 36);  // 29 * 1.25 = 36.25
  EXPECT_EQ(ttfScaleUnits(m, 1000, 29), 29);
  EXPECT_EQ(ttfScaleUnits(m, -250, 29), -7);  // -7.25 rounds toward -7
  EXPECT_EQ(ttfScaleUnits(m, 500, 29), 15);   // 14.5 rounds up
}

// --- 2-bit packing ----------------------------------------------------------------------------

TEST(TtfPack2Bit, QuantisesLikeFontconvert) {
  const uint8_t cov[] = {0, 63, 64, 191, 192};  // levels 0 0 1 2 3
  uint8_t out[2] = {0xAA, 0xAA};
  ASSERT_EQ(packed2BitBytes(5, 1), 2u);
  pack2Bit(cov, 5, 1, out);
  EXPECT_EQ(out[0], 0x06);  // 00 00 01 10
  EXPECT_EQ(out[1], 0xC0);  // 11 then zero padding
}

TEST(TtfPack2Bit, InkWeightShiftsCoverageThresholdMonotonically) {
  // Weight 0 is byte-identical to the default coverage >> 6 quantization.
  uint8_t defOut[64] = {};
  uint8_t zeroOut[64] = {};
  uint8_t allCov[256];
  for (int i = 0; i < 256; ++i) allCov[i] = static_cast<uint8_t>(i);
  pack2Bit(allCov, 256, 1, defOut);
  pack2Bit(allCov, 256, 1, zeroOut, 0);
  EXPECT_EQ(memcmp(defOut, zeroOut, sizeof(defOut)), 0);

  // Pure background (coverage 0) stays white at every weight, including +2.
  for (int8_t w = -3; w <= 3; ++w) {
    EXPECT_EQ(quantizeCoverage2Bit(0, w), 0u) << "weight=" << static_cast<int>(w);
  }

  // First coverage value that crosses into ink (level > 0) drops strictly from -2 to +2:
  // -2: 128, -1: 96, 0: 64, +1: 32, +2: 1.
  const uint8_t expectedCutoff[5] = {128, 96, 64, 32, 1};
  for (int idx = 0; idx < 5; ++idx) {
    const int8_t w = static_cast<int8_t>(idx - 2);
    const uint8_t cut = expectedCutoff[idx];
    EXPECT_EQ(quantizeCoverage2Bit(static_cast<uint8_t>(cut - 1), w), 0u) << "weight=" << static_cast<int>(w);
    EXPECT_GT(quantizeCoverage2Bit(cut, w), 0u) << "weight=" << static_cast<int>(w);
  }

  // Out-of-range weights clamp to [-2, +2].
  EXPECT_EQ(quantizeCoverage2Bit(100, -5), quantizeCoverage2Bit(100, -2));
  EXPECT_EQ(quantizeCoverage2Bit(10, 5), quantizeCoverage2Bit(10, 2));
}

TEST(TtfPack2Bit, RowsPackBackToBackWithoutPadding) {
  const uint8_t cov[] = {255, 255, 255, 255, 0, 0, 0, 0, 255, 0, 255};  // 11 px
  ASSERT_EQ(packed2BitBytes(11, 1), 3u);
  uint8_t out[3];
  pack2Bit(cov, 11, 1, out);
  EXPECT_EQ(out[0], 0xFF);
  EXPECT_EQ(out[1], 0x00);
  EXPECT_EQ(out[2], 0xCC);  // 11 00 11 then pad 00
  // Same bytes whether the caller calls it 11x1 or a 3-row glyph of odd width.
  uint8_t out2[3];
  pack2Bit(cov, 1, 11, out2);
  EXPECT_EQ(memcmp(out, out2, 3), 0);
  EXPECT_EQ(packed2BitBytes(3, 3), 3u);
  EXPECT_EQ(packed2BitBytes(0, 0), 0u);
}

// --- whole-face check (checkTtfFace) ------------------------------------------------------

namespace {
struct FaceSpec {
  uint32_t sfnt = 0x00010000u;
  uint16_t upem = 1000;
  int16_t asc = 800, desc = -200, gap = 0;
  uint16_t numGlyphs = 4;
  uint16_t longMetrics = 2;
  uint16_t locaFormat = 0;
  const char* drop = nullptr;  // table tag to leave out
  bool cff = false;            // CFF outlines (OTTO) instead of glyf/loca
};

// A directory and the tables checkTtfFace reads, each sized exactly as the face needs.
std::vector<uint8_t> makeWholeFace(const FaceSpec& f) {
  struct T {
    const char* tag;
    size_t len;
  };
  std::vector<T> tables = {
      {"head", 54},
      {"hhea", 36},
      {"maxp", 6},
      {"cmap", 4},
      {"hmtx", f.longMetrics * 4u + (f.numGlyphs > f.longMetrics ? (f.numGlyphs - f.longMetrics) * 2u : 0u)}};
  if (f.cff) {
    tables.push_back({"CFF ", 8});
  } else {
    tables.push_back({"loca", (f.numGlyphs + 1u) * (f.locaFormat == 0 ? 2u : 4u)});
    tables.push_back({"glyf", 16});
  }
  std::vector<T> kept;
  for (const T& t : tables)
    if (f.drop == nullptr || strcmp(f.drop, t.tag) != 0) kept.push_back(t);

  size_t at = 12 + 16 * kept.size();
  std::vector<uint8_t> v(at, 0);
  put32(v, 0, f.sfnt);
  put16(v, 4, static_cast<uint16_t>(kept.size()));
  std::vector<size_t> offs;
  for (size_t i = 0; i < kept.size(); ++i) {
    memcpy(&v[12 + 16 * i], kept[i].tag, 4);
    put32(v, 12 + 16 * i + 8, static_cast<uint32_t>(at));
    put32(v, 12 + 16 * i + 12, static_cast<uint32_t>(kept[i].len));
    offs.push_back(at);
    at += kept[i].len;
  }
  v.resize(at, 0);
  for (size_t i = 0; i < kept.size(); ++i) {
    const size_t o = offs[i];
    if (!strcmp(kept[i].tag, "head")) {
      put16(v, o + 18, f.upem);
      put16(v, o + 50, f.locaFormat);
    } else if (!strcmp(kept[i].tag, "hhea")) {
      put16(v, o + 4, static_cast<uint16_t>(f.asc));
      put16(v, o + 6, static_cast<uint16_t>(f.desc));
      put16(v, o + 8, static_cast<uint16_t>(f.gap));
      put16(v, o + 34, f.longMetrics);
    } else if (!strcmp(kept[i].tag, "maxp")) {
      put16(v, o + 4, f.numGlyphs);
    }
  }
  return v;
}

TtfReject check(const std::vector<uint8_t>& v) {
  TtfMetrics m;
  return checkTtfFace(v.data(), v.size(), m);
}
}  // namespace

TEST(TtfFaceCheck, AcceptsAWholeFace) {
  TtfMetrics m;
  const auto v = makeWholeFace({});
  EXPECT_EQ(checkTtfFace(v.data(), v.size(), m), TtfReject::None);
  EXPECT_EQ(m.unitsPerEm, 1000);
  FaceSpec otf;
  otf.sfnt = 0x4F54544Fu;
  otf.cff = true;
  EXPECT_EQ(check(makeWholeFace(otf)), TtfReject::None);
  FaceSpec longLoca;
  longLoca.locaFormat = 1;
  EXPECT_EQ(check(makeWholeFace(longLoca)), TtfReject::None);
}

TEST(TtfFaceCheck, AFileCutShortIsTruncated) {
  const auto v = makeWholeFace({});
  for (const size_t cut : {size_t{13}, size_t{40}, v.size() / 2, v.size() - 1}) {
    const std::vector<uint8_t> shorter(v.begin(), v.begin() + static_cast<long>(cut));
    EXPECT_EQ(check(shorter), TtfReject::Truncated) << "cut at " << cut;
  }
  EXPECT_EQ(check(std::vector<uint8_t>(5, 0)), TtfReject::NotSfnt);
}

TEST(TtfFaceCheck, EveryTableMustLieInsideTheFile) {
  auto v = makeWholeFace({});
  put32(v, 12 + 8, 0x7FFFFFF0u);  // first table starts far past the end
  EXPECT_EQ(check(v), TtfReject::Truncated);
  auto wrap = makeWholeFace({});
  put32(wrap, 12 + 8, 20);
  put32(wrap, 12 + 12, 0xFFFFFFF0u);  // offset + length would wrap a 32-bit sum
  EXPECT_EQ(check(wrap), TtfReject::Truncated);
}

TEST(TtfFaceCheck, RequiredTablesMustBePresent) {
  for (const char* tag : {"head", "hhea", "maxp", "cmap", "hmtx", "loca", "glyf"}) {
    FaceSpec s;
    s.drop = tag;
    EXPECT_EQ(check(makeWholeFace(s)), TtfReject::MissingTable) << tag;
  }
  FaceSpec noCff;
  noCff.sfnt = 0x4F54544Fu;
  noCff.cff = true;
  noCff.drop = "CFF ";
  EXPECT_EQ(check(makeWholeFace(noCff)), TtfReject::MissingTable);
}

TEST(TtfFaceCheck, GlyphCountMustFitTheTables) {
  FaceSpec manyGlyphs;
  manyGlyphs.numGlyphs = 4;
  auto v = makeWholeFace(manyGlyphs);
  // maxp now claims 5000 glyphs, but hmtx and loca were sized for 4.
  const size_t maxpAt = 12 + 16 * 7 + 54 + 36;
  put16(v, maxpAt + 4, 5000);
  EXPECT_EQ(check(v), TtfReject::Truncated);
  FaceSpec none;
  none.numGlyphs = 0;
  none.longMetrics = 0;
  EXPECT_EQ(check(makeWholeFace(none)), TtfReject::BadMetrics);
}

TEST(TtfFaceCheck, ImplausibleMetricsAreRefused) {
  FaceSpec s;
  s.upem = 8;
  EXPECT_EQ(check(makeWholeFace(s)), TtfReject::BadMetrics);
  s = {};
  s.upem = 20000;
  EXPECT_EQ(check(makeWholeFace(s)), TtfReject::BadMetrics);
  s = {};
  s.asc = -200;
  s.desc = 800;
  EXPECT_EQ(check(makeWholeFace(s)), TtfReject::BadMetrics);
  s = {};
  s.asc = 30000;  // a 31-em line: would be rasterised at hundreds of pixels
  s.desc = -1000;
  EXPECT_EQ(check(makeWholeFace(s)), TtfReject::BadMetrics);
  s = {};
  s.gap = -50;
  EXPECT_EQ(check(makeWholeFace(s)), TtfReject::BadMetrics);
  s = {};
  s.longMetrics = 9;  // more long metrics than glyphs
  EXPECT_EQ(check(makeWholeFace(s)), TtfReject::BadMetrics);
  s = {};
  s.locaFormat = 2;
  EXPECT_EQ(check(makeWholeFace(s)), TtfReject::BadMetrics);
}
