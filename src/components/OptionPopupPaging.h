#pragma once
#include <vector>

// Which rows of an over-long option pop-up are on screen. A pure function of the selection,
// so the painter and the hit test cannot disagree and nothing has to remember a scroll
// position: stepping off the last row of a page lands on the next page by itself.
namespace option_popup {

struct Page {
  int first = 0;
  int count = 0;
  int number = 1;
  int total = 1;
};

// Fills pages greedily in row order. A row taller than the list still gets a page of its own.
inline Page pageFor(const std::vector<int>& rowHeights, const int spacing, const int listHeight, const int selected) {
  Page result;
  bool found = false;
  Page current;
  int used = 0;
  for (int i = 0; i < static_cast<int>(rowHeights.size()); ++i) {
    int needed = rowHeights[i] + (current.count > 0 ? spacing : 0);
    if (current.count > 0 && used + needed > listHeight) {
      if (!found && selected < current.first + current.count) {
        result = current;
        found = true;
      }
      current = Page{i, 0, current.number + 1, 1};
      used = 0;
      needed = rowHeights[i];
    }
    used += needed;
    current.count++;
  }
  if (!found) result = current;
  result.total = current.number;
  return result;
}

}  // namespace option_popup
