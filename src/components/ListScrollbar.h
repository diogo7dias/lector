#pragma once

#include <algorithm>

// Where the list's scroll indicator sits. A thin track down the right-hand edge of the
// list with a thumb as long as the visible fraction: it says how far down the list you
// are and how much of it you are looking at, which a pair of up/down arrows could not.
// Pure geometry so the theme stays a draw loop and the answer can be tested on the host.
namespace list_scrollbar {

constexpr int kWidth = 4;
constexpr int kRightMargin = 6;
// Below this the thumb reads as a speck rather than a position. Long lists (a font list,
// a library) hit it, and a thumb that keeps its length still tracks position by moving.
constexpr int kMinThumbHeight = 16;
// The white margin knocked out around the track. Headings and the selected row are solid
// black bars that run the full width of the list, and a black thumb on a black bar shows
// nothing at all; the outline keeps the indicator readable wherever it lands.
constexpr int kOutlineWidth = 1;
// What the list has to give up on its right-hand side so rows never run under the track.
constexpr int kReservedWidth = kWidth + kOutlineWidth * 2 + kRightMargin * 2;

struct Bar {
  bool visible;
  int trackY;
  int trackHeight;
  int thumbY;
  int thumbHeight;
};

inline int trackX(const int listX, const int listWidth) { return listX + listWidth - kRightMargin - kWidth; }

// listY/listHeight are the rows' own band, not the whole screen: the track lines up with
// the first and last row, so a full thumb means "this is the whole list".
inline Bar forList(const int listY, const int listHeight, const int itemCount, const int windowStart,
                   const int visibleRows) {
  if (listHeight <= 0 || visibleRows <= 0 || itemCount <= visibleRows) {
    return Bar{false, listY, listHeight, listY, 0};
  }

  const int thumbHeight =
      std::max(kMinThumbHeight, std::min(listHeight, static_cast<int>(static_cast<long long>(listHeight) * visibleRows /
                                                                      itemCount)));
  const int travel = listHeight - thumbHeight;
  const int lastStart = itemCount - visibleRows;  // > 0: itemCount > visibleRows above
  const int start = std::clamp(windowStart, 0, lastStart);
  // Rounded, not truncated, so the thumb reaches the bottom of the track on the last
  // window instead of stopping a pixel short of it.
  const int offset = static_cast<int>((static_cast<long long>(travel) * start + lastStart / 2) / lastStart);

  return Bar{true, listY, listHeight, listY + offset, thumbHeight};
}

}  // namespace list_scrollbar
