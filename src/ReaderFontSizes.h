#pragma once

#include <SdCardFontRegistry.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

#include "fontIds.h"

// Reader font size is stored as an actual point size (see CrossPointSettings::
// fontPointSize), not an abstract Small/Medium/Large slot. The selectable sizes
// therefore come from whichever family is active: the built-in set below, or the
// .cpfont files a user installed for an SD family.

// The built-in ChareInk family is compiled in at exactly this point size (see the global
// font objects in main.cpp). One size, not four: ChareInk carries a far larger glyph set
// than the Vollkorn it replaced, and four of them would not fit the app partition. The
// other sizes are installed from the card like any other family, and an installed ChareInk
// shadows this one outright — see readerFontPointSizes below.
inline constexpr uint8_t BUILTIN_READER_POINT_SIZES[] = {14};

// Selects a built-in size without loading it; returns 0 for other font families.
inline int builtinReaderFontVariant(const int fontId, const float scale) {
  static constexpr int FAMILIES[][std::size(BUILTIN_READER_POINT_SIZES)] = {
      {CHAREINK_14_FONT_ID},
  };
  for (const int* family : FAMILIES) {
    const int* end = family + std::size(BUILTIN_READER_POINT_SIZES);
    const int* base = std::find(family, end, fontId);
    if (base == end) continue;
    const float target = BUILTIN_READER_POINT_SIZES[base - family] * scale;
    size_t pick = scale < 1.0f ? 0 : static_cast<size_t>(base - family);
    for (size_t i = 0; i < std::size(BUILTIN_READER_POINT_SIZES); ++i) {
      const float points = BUILTIN_READER_POINT_SIZES[i];
      // Shrinking rounds down; growing takes the nearest available size.
      if (scale < 1.0f ? points <= target
                       : std::fabs(points - target) < std::fabs(BUILTIN_READER_POINT_SIZES[pick] - target)) {
        pick = i;
      }
    }
    return family[pick];
  }
  return 0;
}

// Point sizes selectable for the active reader font, ascending: the SD family's
// installed sizes when `sdFamilyName` names one the registry knows, otherwise
// the built-in set. Never returns empty.
std::vector<uint8_t> readerFontPointSizes(const SdCardFontRegistry* registry, const char* sdFamilyName);

// Closest entry in `sizes` (ascending, `count` > 0) to `pt`; ties resolve to the
// smaller size. Takes a raw range rather than a vector because getReaderFontId()
// runs inside the page render loop and must not allocate.
uint8_t snapToNearestPointSize(const uint8_t* sizes, size_t count, uint8_t pt);

inline uint8_t snapToNearestPointSize(const std::vector<uint8_t>& sizes, const uint8_t pt) {
  return sizes.empty() ? pt : snapToNearestPointSize(sizes.data(), sizes.size(), pt);
}
