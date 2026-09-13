#pragma once

// Balance the text's line boxes inside the reading viewport. A full page has
// less than one line advance left; short pages and overflowing content stay put.
constexpr int fullTextPageOffset(int viewportHeight, int firstTop, int lastTop, int lineAdvance,
                                 int naturalLineHeight) {
  if (lineAdvance <= 0 || naturalLineHeight <= 0 || firstTop < 0 || lastTop <= firstTop ||
      viewportHeight - (lastTop - firstTop + lineAdvance) >= lineAdvance) {
    return 0;
  }
  const int bottom = lastTop + naturalLineHeight;
  if (bottom > viewportHeight) return 0;
  return (viewportHeight - (bottom - firstTop)) / 2 - firstTop;
}
