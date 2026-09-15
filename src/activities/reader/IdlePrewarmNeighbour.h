#pragma once

// Which page idle glyph prewarm should scan.
//
// Same-spine motion that went backward prewarms the previous page; everything
// else keeps the existing forward neighbour. Cross-chapter prewarm stays out of
// scope (a spine change is treated as forward). Returns -1 when that neighbour
// is off the section.
inline int idlePrewarmNeighbour(const int currentPage, const int lastPrewarmPage, const int lastPrewarmSpine,
                                const int currentSpine, const int pageCount) {
  int delta = 1;
  if (lastPrewarmSpine == currentSpine && lastPrewarmPage >= 0 && currentPage < lastPrewarmPage) {
    delta = -1;
  }
  const int neighbour = currentPage + delta;
  if (neighbour < 0 || neighbour >= pageCount) return -1;
  return neighbour;
}
