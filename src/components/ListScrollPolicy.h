#pragma once

#include <algorithm>

// Pure windowing logic for lists that scroll instead of paginate (no hardware
// deps — host testable). BaseTheme::drawList normally snaps its visible window
// to whole pages, so stepping one row past the bottom replaces the entire
// screen. A caller that hands drawList a scroll offset gets this behaviour
// instead: the window slides by the least amount that keeps the selected row
// visible, so the rows around the cursor stay put as it moves.
//
// The offset is owned by the caller and survives between frames; drawList
// feeds it back through here every time it draws.
namespace list_scroll {

// Returns the window start that keeps selectedIndex on screen, moving the
// window as little as possible. A dirty or stale offset (list shrank under it,
// selection jumped, caller never initialised it) is always clamped into range,
// so this is safe to call with anything.
inline int nextScrollOffset(int scrollOffset, int selectedIndex, int pageItems, int itemCount) {
  if (pageItems <= 0 || itemCount <= 0) return 0;

  // Never scroll so far that blank rows show below the last item.
  const int maxOffset = std::max(0, itemCount - pageItems);
  int offset = std::clamp(scrollOffset, 0, maxOffset);

  // A list drawn without a selection (EpubReaderMenuActivity passes -1) keeps
  // whatever window it had; there is no cursor to follow.
  if (selectedIndex < 0) return offset;

  if (selectedIndex < offset) {
    offset = selectedIndex;  // selection went above the window
  } else if (selectedIndex >= offset + pageItems) {
    offset = selectedIndex - pageItems + 1;  // selection went below it
  }

  return std::clamp(offset, 0, maxOffset);
}

}  // namespace list_scroll
