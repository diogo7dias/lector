#pragma once

// Where a list says "there is more". Two small chevrons, one at each end of the list
// band: the up one only while rows sit above the window, the down one only while rows
// sit below it. When nothing is left to scroll in a direction that chevron is simply
// absent. Pure geometry so the theme stays a draw loop and the answer can be tested on
// the host.
//
// They replaced a track-and-thumb scrollbar. The chevrons sit in the vertical spacing
// above and below the rows, so no row gives up any width to them.
namespace list_scrollbar {

// Chevron footprint: kWidth px wide, kHeight px tall, stroke kStroke px. Drawn with the
// renderer's line primitive, no bitmap.
constexpr int kWidth = 9;
constexpr int kHeight = 5;
constexpr int kStroke = 2;
// Blank between the chevron and the row band it points away from.
constexpr int kGap = 2;
// How far the chevron's right edge sits in from the list's right edge: centred in the
// 20 px side padding every list keeps.
constexpr int kRightInset = 6;

struct Arrows {
  bool up;
  bool down;
};

// windowStart is the index of the first row on screen, visibleRows how many rows the
// window holds (measured, when rows vary in height). Both arrows are off when the whole
// list fits.
inline Arrows forWindow(const int itemCount, const int windowStart, const int visibleRows) {
  if (itemCount <= 0 || visibleRows <= 0 || itemCount <= visibleRows) return Arrows{false, false};
  const int start = windowStart < 0 ? 0 : windowStart;
  return Arrows{start > 0, start + visibleRows < itemCount};
}

// Same answer for a list that reports the index range it actually drew.
inline Arrows forVisibleRange(const int itemCount, const int firstVisible, const int lastVisible) {
  if (itemCount <= 0) return Arrows{false, false};
  return Arrows{firstVisible > 0, lastVisible < itemCount - 1};
}

// Which screen of rows the window shows, for the "2/5" beside the battery: the chevrons
// say there is more, this says where. pages is 0 when the whole list fits.
struct Page {
  int page;
  int pages;
};

inline Page pageOf(const int itemCount, const int windowStart, const int visibleRows) {
  if (itemCount <= 0 || visibleRows <= 0 || itemCount <= visibleRows) return Page{0, 0};
  const int pages = (itemCount + visibleRows - 1) / visibleRows;
  const int start = windowStart < 0 ? 0 : windowStart;
  // The last page only once the last row is on screen; before that, the page the top row
  // falls on, so a window one row in still reads 1.
  if (start + visibleRows >= itemCount) return Page{pages, pages};
  const int page = start / visibleRows + 1;
  return Page{page < pages ? page : pages - 1, pages};
}

// The band the chevrons sit in: the rows grown into the vertical spacing above and
// below them, never further than a chevron and its gap need. Any {x, y, width, height}
// rect type.
template <typename R>
R outsideBand(const R& rows, const int spacing) {
  const int reach = spacing < kHeight + kGap ? spacing : kHeight + kGap;
  return R{rows.x, rows.y - reach, rows.width, rows.height + reach * 2};
}

}  // namespace list_scrollbar
