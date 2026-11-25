// KernSparseTest: the built-in fonts store kerning as split class maps plus a sparse (CSR)
// matrix, SD-card fonts as packed class maps plus a dense matrix. Both must answer
// EpdFont::getKerning identically, and the built-in tables must expand back to exactly the
// dense matrices they were converted from.
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

#include "EpdFont.h"
#include "EpdFontData.h"
#include "Utf8.h"
#include "builtinFonts/chareink_14_bold.h"
#include "builtinFonts/chareink_14_bolditalic.h"
#include "builtinFonts/chareink_14_italic.h"
#include "builtinFonts/chareink_14_regular.h"

namespace {

// --- the dense form, rebuilt from a split+sparse font -------------------------------------

struct Dense {
  std::vector<uint8_t> left, right;  // classId by codepoint index
  std::vector<int8_t> matrix;        // leftClassCount x rightClassCount
};

std::vector<int8_t> expandSparse(const EpdFontData& d) {
  std::vector<int8_t> dense(static_cast<size_t>(d.kernLeftClassCount) * d.kernRightClassCount, 0);
  for (uint16_t row = 0; row < d.kernLeftClassCount; ++row) {
    for (uint16_t i = d.kernRowOffsets[row]; i < d.kernRowOffsets[row + 1]; ++i) {
      dense[static_cast<size_t>(row) * d.kernRightClassCount + d.kernSparseCols[i]] = d.kernSparseValues[i];
    }
  }
  return dense;
}

// FNV-1a over: left entries (cp lo, cp hi, class), '|', right entries, the two class counts, then
// the dense matrix as bytes. The reference values below were computed from the dense headers
// before the conversion, so a match proves the sparse form carries the identical table.
uint32_t expansionHash(const EpdFontData& d) {
  uint32_t h = 0x811C9DC5u;
  const auto mix = [&h](const uint8_t b) { h = (h ^ b) * 0x01000193u; };
  for (uint16_t i = 0; i < d.kernLeftEntryCount; ++i) {
    mix(d.kernLeftCodepoints[i] & 0xFF);
    mix(d.kernLeftCodepoints[i] >> 8);
    mix(d.kernLeftClassIds[i]);
  }
  mix('|');
  for (uint16_t i = 0; i < d.kernRightEntryCount; ++i) {
    mix(d.kernRightCodepoints[i] & 0xFF);
    mix(d.kernRightCodepoints[i] >> 8);
    mix(d.kernRightClassIds[i]);
  }
  mix(d.kernLeftClassCount);
  mix(d.kernRightClassCount);
  for (const int8_t v : expandSparse(d)) mix(static_cast<uint8_t>(v));
  return h;
}

struct Pinned {
  const char* name;
  const EpdFontData* font;
  uint32_t hash;
};

const Pinned kBuiltIns[] = {
    {"chareink_14_regular", &chareink_14_regular, 3511620311u},
    {"chareink_14_bold", &chareink_14_bold, 2988454756u},
    {"chareink_14_italic", &chareink_14_italic, 882595177u},
    {"chareink_14_bolditalic", &chareink_14_bolditalic, 3994623942u},
};

TEST(KernSparse, BuiltInTablesExpandToTheOriginalDenseMatrices) {
  for (const Pinned& p : kBuiltIns) {
    SCOPED_TRACE(p.name);
    EXPECT_EQ(expansionHash(*p.font), p.hash);
  }
}

TEST(KernSparse, BuiltInTablesAreWellFormed) {
  for (const Pinned& p : kBuiltIns) {
    SCOPED_TRACE(p.name);
    const EpdFontData& d = *p.font;
    // Built-ins carry the split and sparse forms and none of the packed or dense ones.
    EXPECT_EQ(d.kernLeftClasses, nullptr);
    EXPECT_EQ(d.kernRightClasses, nullptr);
    EXPECT_EQ(d.kernMatrix, nullptr);
    ASSERT_NE(d.kernLeftCodepoints, nullptr);
    ASSERT_NE(d.kernRowOffsets, nullptr);

    for (uint16_t i = 1; i < d.kernLeftEntryCount; ++i) EXPECT_LT(d.kernLeftCodepoints[i - 1], d.kernLeftCodepoints[i]);
    for (uint16_t i = 1; i < d.kernRightEntryCount; ++i)
      EXPECT_LT(d.kernRightCodepoints[i - 1], d.kernRightCodepoints[i]);
    for (uint16_t i = 0; i < d.kernLeftEntryCount; ++i) {
      EXPECT_GE(d.kernLeftClassIds[i], 1);
      EXPECT_LE(d.kernLeftClassIds[i], d.kernLeftClassCount);
    }
    for (uint16_t i = 0; i < d.kernRightEntryCount; ++i) {
      EXPECT_GE(d.kernRightClassIds[i], 1);
      EXPECT_LE(d.kernRightClassIds[i], d.kernRightClassCount);
    }
    EXPECT_EQ(d.kernRowOffsets[0], 0);
    for (uint16_t row = 0; row < d.kernLeftClassCount; ++row) {
      ASSERT_LE(d.kernRowOffsets[row], d.kernRowOffsets[row + 1]);
      for (uint16_t i = d.kernRowOffsets[row]; i < d.kernRowOffsets[row + 1]; ++i) {
        EXPECT_LT(d.kernSparseCols[i], d.kernRightClassCount);
        EXPECT_NE(d.kernSparseValues[i], 0);  // zeros are the absent cells
        if (i > d.kernRowOffsets[row]) {
          EXPECT_LT(d.kernSparseCols[i - 1], d.kernSparseCols[i]);
        }
      }
    }
  }
}

// getKerning on the real tables equals a lookup in the expanded dense matrix, for every pair of
// codepoints the class maps know plus codepoints they do not.
TEST(KernSparse, GetKerningMatchesTheExpandedMatrixForEveryBuiltInPair) {
  for (const Pinned& p : kBuiltIns) {
    SCOPED_TRACE(p.name);
    const EpdFontData& d = *p.font;
    const EpdFont font(&d);
    const std::vector<int8_t> dense = expandSparse(d);
    std::vector<uint32_t> lefts(d.kernLeftCodepoints, d.kernLeftCodepoints + d.kernLeftEntryCount);
    std::vector<uint32_t> rights(d.kernRightCodepoints, d.kernRightCodepoints + d.kernRightEntryCount);
    lefts.push_back(0x2E80);  // not in any class map (and CJK, which never kerns)
    lefts.push_back(0x1F600);
    rights.push_back(0x0001);
    rights.push_back(0x1F600);
    size_t nonZero = 0;
    for (const uint32_t l : lefts) {
      for (const uint32_t r : rights) {
        uint8_t lc = 0, rc = 0;
        for (uint16_t i = 0; i < d.kernLeftEntryCount && l <= 0xFFFF; ++i)
          if (d.kernLeftCodepoints[i] == l) lc = d.kernLeftClassIds[i];
        for (uint16_t i = 0; i < d.kernRightEntryCount && r <= 0xFFFF; ++i)
          if (d.kernRightCodepoints[i] == r) rc = d.kernRightClassIds[i];
        const int8_t want = (lc && rc && !utf8IsCjkBreakable(l) && !utf8IsCjkBreakable(r))
                                ? dense[static_cast<size_t>(lc - 1) * d.kernRightClassCount + (rc - 1)]
                                : 0;
        ASSERT_EQ(font.getKerning(l, r), want) << std::hex << "left U+" << l << " right U+" << r;
        nonZero += want != 0;
      }
    }
    EXPECT_GT(nonZero, 0u);
  }
}

// --- both representations of one random matrix answer alike -------------------------------

TEST(KernSparse, DenseAndSparseAnswerAlikeOnRandomTables) {
  std::mt19937 rng(1234);
  for (int round = 0; round < 20; ++round) {
    const uint8_t lcCount = 1 + rng() % 40;
    const uint8_t rcCount = 1 + rng() % 40;
    // Class maps: ascending codepoints, class ids in range.
    std::vector<EpdKernClassEntry> left, right;
    std::vector<uint16_t> lcp, rcp;
    std::vector<uint8_t> lid, rid;
    for (uint16_t cp = 0x20; cp < 0x120; cp += 1 + rng() % 3) {
      if (rng() % 4 != 0) {
        const uint8_t id = 1 + rng() % lcCount;
        left.push_back({cp, id});
        lcp.push_back(cp);
        lid.push_back(id);
      }
      if (rng() % 4 != 0) {
        const uint8_t id = 1 + rng() % rcCount;
        right.push_back({cp, id});
        rcp.push_back(cp);
        rid.push_back(id);
      }
    }
    std::vector<int8_t> dense(static_cast<size_t>(lcCount) * rcCount, 0);
    for (int8_t& v : dense) v = (rng() % 5 == 0) ? static_cast<int8_t>(1 + rng() % 127) * (rng() % 2 ? 1 : -1) : 0;
    std::vector<uint16_t> offsets;
    std::vector<uint8_t> cols;
    std::vector<int8_t> vals;
    for (uint8_t row = 0; row < lcCount; ++row) {
      offsets.push_back(static_cast<uint16_t>(cols.size()));
      for (uint8_t col = 0; col < rcCount; ++col) {
        const int8_t v = dense[static_cast<size_t>(row) * rcCount + col];
        if (v != 0) {
          cols.push_back(col);
          vals.push_back(v);
        }
      }
    }
    offsets.push_back(static_cast<uint16_t>(cols.size()));

    EpdFontData packed;
    std::memset(&packed, 0, sizeof(packed));
    packed.kernLeftClasses = left.data();
    packed.kernRightClasses = right.data();
    packed.kernMatrix = dense.data();
    packed.kernLeftEntryCount = static_cast<uint16_t>(left.size());
    packed.kernRightEntryCount = static_cast<uint16_t>(right.size());
    packed.kernLeftClassCount = lcCount;
    packed.kernRightClassCount = rcCount;

    EpdFontData sparse;
    std::memset(&sparse, 0, sizeof(sparse));
    sparse.kernLeftCodepoints = lcp.data();
    sparse.kernLeftClassIds = lid.data();
    sparse.kernRightCodepoints = rcp.data();
    sparse.kernRightClassIds = rid.data();
    sparse.kernRowOffsets = offsets.data();
    sparse.kernSparseCols = cols.data();
    sparse.kernSparseValues = vals.data();
    sparse.kernLeftEntryCount = packed.kernLeftEntryCount;
    sparse.kernRightEntryCount = packed.kernRightEntryCount;
    sparse.kernLeftClassCount = lcCount;
    sparse.kernRightClassCount = rcCount;

    const EpdFont a(&packed);
    const EpdFont b(&sparse);
    for (uint32_t l = 0x10; l < 0x130; ++l) {
      for (uint32_t r = 0x10; r < 0x130; ++r) {
        ASSERT_EQ(a.getKerning(l, r), b.getKerning(l, r)) << "round " << round << " " << l << "," << r;
      }
    }
  }
}

TEST(KernSparse, FontsWithoutKerningReturnZero) {
  EpdFontData none;
  std::memset(&none, 0, sizeof(none));
  EXPECT_EQ(EpdFont(&none).getKerning('A', 'V'), 0);
}

}  // namespace
