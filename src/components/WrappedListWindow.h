#pragma once

#include <algorithm>
#include <functional>

// Pure windowing logic for BaseTheme::drawWrappedList (no hardware deps — host testable).
//
// Rows in a wrapped list vary in height, because a long title wraps over as many lines as
// it needs. So the window cannot be "N rows of a fixed step": it is however many rows fit
// in the available height, starting from wherever the caller's scroll offset points, and
// it must always contain the selected row or the user cannot see what they are about to
// open.
//
// The two rules together are what make this worth its own file. Fitting forward from the
// offset is easy; keeping the selection inside when it has moved below the window means
// walking the window BACK from the selection, and the backward walk has to obey exactly
// the same limits as the forward one or it lands on a start the forward pass will not
// reach — which draws a window with no visible selection.
namespace wrapped_list {

struct Window {
  int first;  // index of the first row to draw
  int count;  // how many rows fit, always at least 1 when itemCount > 0
};

// heightOf(index) is the row's full drawn height, its own padding included. Rows are laid
// out with rowGap between them and none before the first. listHeight is the space the rows
// may occupy, i.e. the rect minus whatever the caller reserved for its "N more" badges.
//
// heightOf is called only for rows at or near the window, never for the whole list: a
// directory can hold thousands of entries and measuring one means wrapping its title.
inline Window window(const int itemCount, const int selectedIndex, const int scrollOffset, const int listHeight,
                     const int rowGap, const std::function<int(int index)>& heightOf) {
  if (itemCount <= 0 || !heightOf) return {0, 0};

  // How many rows fit going forward from `from`, greedily. Always reports at least one
  // row: a single row taller than the whole list is still drawn (clipped) rather than
  // leaving the caller with a blank screen.
  auto fitForward = [&](const int from) {
    int used = 0;
    int count = 0;
    for (int i = from; i < itemCount; i++) {
      const int step = heightOf(i) + (count == 0 ? 0 : rowGap);
      if (count > 0 && used + step > listHeight) break;
      used += step;
      count++;
    }
    return std::max(count, 1);
  };

  int start = std::clamp(scrollOffset, 0, itemCount - 1);
  // Selection above the window: it becomes the top row, and everything below follows.
  if (selectedIndex >= 0 && selectedIndex < start) start = selectedIndex;

  int count = fitForward(start);

  // Selection below the window: walk the start back from the selection while the rows
  // above still fit, which lands the selected row at the bottom. The walk stops on the
  // same height limit the forward pass uses, so the forward pass from this new start is
  // guaranteed to reach the selection again.
  if (selectedIndex >= 0 && selectedIndex > start + count - 1) {
    int newStart = selectedIndex;
    int used = heightOf(selectedIndex);
    while (newStart > 0) {
      const int step = heightOf(newStart - 1) + rowGap;
      if (used + step > listHeight) break;
      used += step;
      newStart--;
    }
    start = newStart;
    count = fitForward(start);
  }

  // Belt and braces. The walk above is meant to guarantee this, but a heightOf that
  // reports different heights for the same row between calls (a font swapped mid-draw,
  // say) could break the guarantee, and a list drawn with no visible cursor is the one
  // outcome that leaves the user unable to tell what they are about to open. Sliding the
  // window down one row at a time always terminates: at start == selectedIndex the
  // selection is the first row, which fitForward always includes.
  while (selectedIndex >= 0 && selectedIndex < itemCount && selectedIndex > start + count - 1 &&
         start < selectedIndex) {
    start++;
    count = fitForward(start);
  }

  return {start, count};
}

}  // namespace wrapped_list
