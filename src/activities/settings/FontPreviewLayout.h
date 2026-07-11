#pragma once

#include <algorithm>

struct FontPreviewRowLayout {
  int labelY = 0;
  int textTop = 0;
  int textBottom = 0;
};

struct FontPreviewLayout {
  FontPreviewRowLayout normal;
  FontPreviewRowLayout paperback;
};

inline FontPreviewLayout calculateFontPreviewLayout(const int top, const int height, const int padding,
                                                    const int labelHeight, const int labelGap, const int rowGap) {
  const int innerTop = top + std::min(std::max(0, padding), std::max(0, height));
  const int innerBottom = std::max(innerTop, top + height - std::max(0, padding));
  const int available = innerBottom - innerTop;
  const int safeRowGap = std::min(std::max(0, rowGap), available);
  const int rowHeight = (available - safeRowGap) / 2;
  const int labelBlockHeight = std::max(0, labelHeight) + std::max(0, labelGap);

  const auto makeRow = [labelBlockHeight](const int rowTop, const int rowBottom) {
    FontPreviewRowLayout row;
    row.labelY = rowTop;
    row.textTop = std::min(rowBottom, rowTop + labelBlockHeight);
    row.textBottom = std::max(row.textTop, rowBottom);
    return row;
  };

  FontPreviewLayout layout;
  const int normalBottom = innerTop + rowHeight;
  const int paperbackTop = normalBottom + safeRowGap;
  layout.normal = makeRow(innerTop, normalBottom);
  layout.paperback = makeRow(paperbackTop, paperbackTop + rowHeight);
  return layout;
}
