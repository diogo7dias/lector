#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "SdCardFont.h"

// load() allocates the resident interval tables with nothrow new[], so counting those calls counts tables.
static int nothrowArrayAllocs = 0;

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
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

}  // namespace
