#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "SdCardFont.h"

// load() allocates the resident interval tables with nothrow new[], so counting those calls counts tables.
static int nothrowArrayAllocs = 0;
// A nonzero size makes the next nothrow new[] of exactly that size fail once.
static size_t failNextArraySize = 0;

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  if (failNextArraySize != 0 && size == failNextArraySize) {
    failNextArraySize = 0;
    return nullptr;
  }
  ++nothrowArrayAllocs;
  return std::malloc(size ? size : 1);
}

namespace {

struct Interval {
  uint32_t first;
  uint32_t last;
};

struct Style {
  uint32_t glyphCount;
  std::vector<Interval> intervals;
};

void putU16(std::vector<uint8_t>& b, size_t at, uint16_t v) {
  b[at] = v & 0xFF;
  b[at + 1] = v >> 8;
}

void putU32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
  for (int i = 0; i < 4; i++) b[at + i] = (v >> (8 * i)) & 0xFF;
}

std::string writeFont(const char* name, const std::vector<Style>& styles) {
  constexpr size_t HEADER = 32;
  constexpr size_t TOC = 32;
  std::vector<uint8_t> b(HEADER + TOC * styles.size(), 0);
  const char magic[8] = {'C', 'P', 'F', 'O', 'N', 'T', '\0', '\0'};
  std::copy(magic, magic + 8, b.begin());
  putU16(b, 8, CPFONT_VERSION);
  b[12] = static_cast<uint8_t>(styles.size());

  for (size_t i = 0; i < styles.size(); i++) {
    const size_t toc = HEADER + TOC * i;
    b[toc] = static_cast<uint8_t>(i);
    putU32(b, toc + 4, static_cast<uint32_t>(styles[i].intervals.size()));
    putU32(b, toc + 8, styles[i].glyphCount);
    b[toc + 12] = 20;
    putU32(b, toc + 24, static_cast<uint32_t>(b.size()));
    uint32_t offset = 0;
    for (const auto& iv : styles[i].intervals) {
      const size_t at = b.size();
      b.resize(at + 12);
      putU32(b, at, iv.first);
      putU32(b, at + 4, iv.last);
      putU32(b, at + 8, offset);
      offset += iv.last - iv.first + 1;
    }
  }

  const std::string path = ::testing::TempDir() + name;
  std::FILE* f = std::fopen(path.c_str(), "wb");
  std::fwrite(b.data(), 1, b.size(), f);
  std::fclose(f);
  return path;
}

bool covers(SdCardFont& font, uint8_t style, uint32_t cp) {
  const EpdFontData* d = font.getEpdFont(style)->data;
  return d->coverageHandler(d->glyphMissCtx, cp);
}

const std::vector<Interval> LATIN = {{0x20, 0x7E}, {0xA0, 0xFF}};
const std::vector<Interval> HANGUL = {{0x20, 0x7E}, {0xAC00, 0xD7A3}};

int allocsToLoad(SdCardFont& font, const std::string& path) {
  nothrowArrayAllocs = 0;
  EXPECT_TRUE(font.load(path.c_str()));
  return nothrowArrayAllocs;
}

TEST(SdCardFontIntervals, IdenticalTablesAreAllocatedOnce) {
  const auto path = writeFont("same.cpfont", {{300, LATIN}, {300, LATIN}, {300, LATIN}});
  SdCardFont font;
  EXPECT_EQ(allocsToLoad(font, path), 1);
  for (uint8_t s = 0; s < 3; s++) {
    EXPECT_TRUE(covers(font, s, 'A'));
    EXPECT_TRUE(covers(font, s, 0xE9));
    EXPECT_FALSE(covers(font, s, 0x100));
  }
}

TEST(SdCardFontIntervals, DifferentTablesKeepTheirOwnCoverage) {
  const auto path = writeFont("diff.cpfont", {{300, LATIN}, {20000, HANGUL}, {300, LATIN}});
  SdCardFont font;
  EXPECT_EQ(allocsToLoad(font, path), 2);
  EXPECT_TRUE(covers(font, 0, 0xE9));
  EXPECT_FALSE(covers(font, 0, 0xAC00));
  EXPECT_TRUE(covers(font, 1, 0xAC00));
  EXPECT_FALSE(covers(font, 1, 0xE9));
  EXPECT_TRUE(covers(font, 2, 0xE9));
}

TEST(SdCardFontIntervals, SameRecordsInDifferentResidentFormsAreNotShared) {
  // 65536 glyphs forces the 12-byte form; the second style fits the compact 6-byte form.
  const auto path = writeFont("forms.cpfont", {{65536, LATIN}, {300, LATIN}});
  SdCardFont font;
  EXPECT_EQ(allocsToLoad(font, path), 2);
  EXPECT_TRUE(covers(font, 0, 0xE9));
  EXPECT_TRUE(covers(font, 1, 0xE9));
}

TEST(SdCardFontIntervals, ReloadAndDestroyFreeSharedTableOnce) {
  const auto same = writeFont("reload-same.cpfont", {{300, LATIN}, {300, LATIN}});
  const auto diff = writeFont("reload-diff.cpfont", {{300, LATIN}, {20000, HANGUL}});
  SdCardFont font;
  EXPECT_EQ(allocsToLoad(font, same), 1);
  EXPECT_EQ(allocsToLoad(font, diff), 2);
  EXPECT_TRUE(covers(font, 1, 0xAC00));
  EXPECT_EQ(allocsToLoad(font, same), 1);
  EXPECT_TRUE(covers(font, 1, 0xE9));
}

// Six Latin glyphs 'A'..'F' plus U+FFFD, with left classes for 'A' and 'C',
// right classes for 'B' and 'D', and a class matrix whose top-left 2x2 corner
// holds their pairs. `extraEntries` appends entries for U+0100 onwards to both
// class tables, so the tables span several read blocks. They use `extraClass`,
// or classes 3..classCount in turn when it is 0. A `classCount` of 0 writes no
// kern data. `ligature` adds one pair, E+F -> A.
std::string writeKerningFont(uint16_t extraEntries = 0, uint8_t classCount = 2, uint8_t extraClass = 2,
                             bool ligature = false) {
  constexpr uint32_t KERN_GLYPHS = 7;
  constexpr uint16_t BITMAP_BYTES = 128;
  constexpr size_t GLYPH_OFFSET = 64 + 24;
  constexpr size_t KERN_OFFSET = GLYPH_OFFSET + KERN_GLYPHS * sizeof(EpdGlyph);
  constexpr uint8_t LEFT[][2] = {{'A', 1}, {'C', 2}};
  constexpr uint8_t RIGHT[][2] = {{'B', 1}, {'D', 2}};
  constexpr int8_t MATRIX[2][2] = {{-3, 0}, {4, -5}};
  const uint16_t entries = classCount ? 2 + extraEntries : 0;
  const size_t matrixBytes = static_cast<size_t>(classCount) * classCount;
  const size_t ligatureOffset = KERN_OFFSET + entries * 3 * 2 + matrixBytes;
  const size_t bitmapOffset = ligatureOffset + (ligature ? 8 : 0);
  std::vector<uint8_t> b(bitmapOffset + KERN_GLYPHS * BITMAP_BYTES, 0);
  std::memcpy(b.data(), "CPFONT\0\0", 8);
  putU16(b, 8, CPFONT_VERSION);
  b[12] = 1;
  putU32(b, 36, 2);
  putU32(b, 40, KERN_GLYPHS);
  b[44] = 32;
  putU16(b, 45, 32);
  putU16(b, 49, entries);  // left class entries
  putU16(b, 51, entries);  // right class entries
  b[53] = classCount;
  b[54] = classCount;
  b[55] = ligature ? 1 : 0;
  putU32(b, 56, 64);
  putU32(b, 64, 'A');
  putU32(b, 68, 'F');
  putU32(b, 76, 0xFFFD);
  putU32(b, 80, 0xFFFD);
  putU32(b, 84, KERN_GLYPHS - 1);
  for (uint32_t i = 0; i < KERN_GLYPHS; ++i) {
    EpdGlyph glyph{};
    glyph.width = 32;
    glyph.height = 32;
    glyph.advanceX = 32 << 4;
    glyph.top = 32;
    glyph.dataLength = BITMAP_BYTES;
    glyph.dataOffset = i * BITMAP_BYTES;
    std::memcpy(b.data() + GLYPH_OFFSET + i * sizeof(glyph), &glyph, sizeof(glyph));
  }
  size_t at = KERN_OFFSET;
  for (const auto* table : {LEFT, RIGHT}) {
    if (classCount == 0) break;
    for (size_t i = 0; i < 2; ++i, at += 3) {
      putU16(b, at, table[i][0]);
      b[at + 2] = table[i][1];
    }
    for (uint16_t i = 0; i < extraEntries; ++i, at += 3) {
      putU16(b, at, 0x100 + i);
      b[at + 2] = extraClass ? extraClass : 3 + i % (classCount - 2);
    }
  }
  for (size_t row = 0; classCount > 0 && row < 2; ++row) {
    std::memcpy(b.data() + at + row * classCount, MATRIX[row], sizeof(MATRIX[row]));
  }
  if (ligature) {
    putU32(b, ligatureOffset, 'E' << 16 | 'F');
    putU32(b, ligatureOffset + 4, 'A');
  }

  const std::string path = ::testing::TempDir() + "kern.cpfont";
  std::FILE* f = std::fopen(path.c_str(), "wb");
  std::fwrite(b.data(), 1, b.size(), f);
  std::fclose(f);
  return path;
}

// `ascii` followed by `count` two-byte UTF-8 codepoints from `first`.
std::string latinPage(const char* ascii, uint32_t first, uint32_t count) {
  std::string text = ascii;
  for (uint32_t cp = first; cp < first + count; ++cp) {
    text.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    text.push_back(static_cast<char>(0x80 | (cp & 63)));
  }
  return text;
}

TEST(SdCardFontKerning, PagesKernWithTheFontsClassMatrix) {
  SdCardFont font;
  ASSERT_TRUE(font.load(writeKerningFont().c_str()));
  ASSERT_EQ(0, font.prewarm("ABCDEF", 1, false, true));
  const EpdFont* epd = font.getEpdFont();
  EXPECT_EQ(-3, epd->getKerning('A', 'B'));
  EXPECT_EQ(0, epd->getKerning('A', 'D'));
  EXPECT_EQ(4, epd->getKerning('C', 'B'));
  EXPECT_EQ(-5, epd->getKerning('C', 'D'));
  EXPECT_EQ(0, epd->getKerning('B', 'D'));
  EXPECT_EQ(0, epd->getKerning('E', 'B'));
}

TEST(SdCardFontKerning, KernRequestsServedFromAKernFreeMiniStillKern) {
  SdCardFont font;
  ASSERT_TRUE(font.load(writeKerningFont().c_str()));
  ASSERT_EQ(0, font.prewarm("ABCDEF", 1, false, false));  // kern-free prewarm, e.g. a UI string
  struct Step {
    const char* text;
    uint32_t left, right;
    int8_t kern;
  };
  // A subset without kerning pairs, then subsets whose pairs the earlier ones did not cover.
  for (const Step& step : {Step{"EF", 'E', 'F', 0}, Step{"AB", 'A', 'B', -3}, Step{"CD", 'C', 'D', -5}}) {
    ASSERT_EQ(0, font.prewarm(step.text, 1, false, true));
    EXPECT_EQ(step.kern, font.getEpdFont()->getKerning(step.left, step.right)) << step.text;
  }
}

TEST(SdCardFontKerning, RedrawsAfterAKernFreeRebuildStillKern) {
  SdCardFont font;
  ASSERT_TRUE(font.load(writeKerningFont().c_str()));
  ASSERT_EQ(0, font.prewarm("ABCD", 1, false, true));
  ASSERT_EQ(0, font.prewarm("ABCDEF", 1, false, false));  // kern-free rebuild, e.g. a UI string
  ASSERT_EQ(0, font.prewarm("ABCD", 1, false, true));     // the page again, served from that cache
  EXPECT_EQ(-3, font.getEpdFont()->getKerning('A', 'B'));
  EXPECT_EQ(-5, font.getEpdFont()->getKerning('C', 'D'));
}

TEST(SdCardFontKerning, ClassIdsPastTheMatrixAreUnkerned) {
  SdCardFont font;
  ASSERT_TRUE(font.load(writeKerningFont(1, 2, 250).c_str()));  // U+0100 claims class 250 in a 2x2 matrix
  ASSERT_EQ(1, font.prewarm(latinPage("ABCD", 0x100, 1).c_str(), 1, false, true));  // U+0100 has no glyph
  const EpdFont* epd = font.getEpdFont();
  EXPECT_EQ(0, epd->getKerning('A', 0x100));
  EXPECT_EQ(0, epd->getKerning(0x100, 'B'));
  EXPECT_EQ(-3, epd->getKerning('A', 'B'));
  EXPECT_EQ(-5, epd->getKerning('C', 'D'));
}

TEST(SdCardFontKerning, PagesCanUseAll255KernClasses) {
  SdCardFont font;
  ASSERT_TRUE(font.load(writeKerningFont(253, 255, 0).c_str()));
  ASSERT_EQ(253, font.prewarm(latinPage("ABCD", 0x100, 253).c_str(), 1, false, true));
  const EpdFont* epd = font.getEpdFont();
  EXPECT_EQ(-3, epd->getKerning('A', 'B'));
  EXPECT_EQ(-5, epd->getKerning('C', 'D'));
  EXPECT_EQ(0, epd->getKerning(0x100, 0x1FC));
}

TEST(SdCardFontKerning, AFailedKernBuildKeepsTheLigatures) {
  SdCardFont font;
  ASSERT_TRUE(font.load(writeKerningFont(253, 255, 0, true).c_str()));
  failNextArraySize = 255 * 255;  // the mini kern matrix
  ASSERT_EQ(253, font.prewarm(latinPage("ABCDEF", 0x100, 253).c_str(), 1, false, true));
  EXPECT_EQ(0U, failNextArraySize);
  const EpdFont* epd = font.getEpdFont();
  EXPECT_EQ(static_cast<uint32_t>('A'), epd->getLigature('E', 'F'));
  EXPECT_EQ(0, epd->getKerning('A', 'B'));
}

TEST(SdCardFontKerning, LigatureRequestsServedFromAKernFreeMiniGetLigatures) {
  SdCardFont font;
  ASSERT_TRUE(font.load(writeKerningFont(0, 0, 2, true).c_str()));  // ligatures, no kern classes
  ASSERT_EQ(0, font.prewarm("AEF", 1, false, false));  // kern-free prewarm, e.g. a UI string
  ASSERT_EQ(0, font.prewarm("EF", 1, false, true));
  EXPECT_EQ(static_cast<uint32_t>('A'), font.getEpdFont()->getLigature('E', 'F'));
}

}  // namespace
