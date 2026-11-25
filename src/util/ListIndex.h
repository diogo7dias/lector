#pragma once

#include <algorithm>

// Where a list's selection lands. Pure arithmetic, so the wrap rule, the clamp
// rule and the page jumps are checkable on the host — ButtonNavigator's own .cpp
// pulls in Arduino (millis) and MappedInputManager, so anything living there can
// only be tested by copying it into the test and grepping the source to catch
// the copy going stale.
//
// Same split as slider_field (components/SliderField.h): the decision is here,
// the input plumbing stays in ButtonNavigator, which forwards to these.
namespace list_index {

// A single press wraps: stepping past the last row reaches the first. Long-
// standing behaviour of the press path, and the reason heldIndex below is a
// separate rule rather than the same one with a different step.
inline int next(const int currentIndex, const int totalItems, const int steps = 1) {
  if (totalItems <= 0) return 0;
  return (currentIndex + steps % totalItems) % totalItems;
}

inline int previous(const int currentIndex, const int totalItems, const int steps = 1) {
  if (totalItems <= 0) return 0;
  return (currentIndex + totalItems - steps % totalItems) % totalItems;
}

// One repeat of a HELD key. Clamped, never wrapped: a hold that wrapped past the
// last row would run forever and could not be aimed, which is the whole
// complaint about holding in a long list. Landing on the first or last row ends
// the travel.
inline int held(const int currentIndex, const int totalItems, const int delta) {
  if (totalItems <= 0) return 0;
  return std::clamp(currentIndex + delta, 0, totalItems - 1);
}

// Where a swipe leaves a list. The window moves a whole screen: the first row of the
// next screen becomes the top row. The selection travels with it, so it is still on
// screen and the next key press steps from there. Clamped at both ends, never wrapped;
// a list that fits on one screen does not move. Hosts keep their own window, so each
// applies this to its own top and selection.
struct Window {
  int top;
  int selected;
};

inline Window paged(const int top, const int selected, const int totalItems, const int rowsPerScreen,
                    const int direction) {
  if (totalItems <= 0 || rowsPerScreen <= 0) return {top, selected};
  const int lastTop = std::max(0, totalItems - rowsPerScreen);
  const int nextTop = std::clamp(top + direction * rowsPerScreen, 0, lastTop);
  const int lastShown = std::min(totalItems, nextTop + rowsPerScreen) - 1;
  return {nextTop, std::clamp(selected + nextTop - top, nextTop, lastShown)};
}

}  // namespace list_index
