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

// A swipe travels a page rather than a row. When everything fits on one page
// there is no page to jump to, so it degrades to the wrapping single step.
inline int nextPage(const int currentIndex, const int totalItems, const int itemsPerPage) {
  if (totalItems <= 0 || itemsPerPage <= 0) return 0;
  if (totalItems <= itemsPerPage) return next(currentIndex, totalItems);

  const int lastPageIndex = (totalItems - 1) / itemsPerPage;
  const int currentPageIndex = currentIndex / itemsPerPage;

  if (currentPageIndex < lastPageIndex) return (currentPageIndex + 1) * itemsPerPage;
  return 0;  // past the last page, back to the top
}

inline int previousPage(const int currentIndex, const int totalItems, const int itemsPerPage) {
  if (totalItems <= 0 || itemsPerPage <= 0) return 0;
  if (totalItems <= itemsPerPage) return previous(currentIndex, totalItems);

  const int lastPageIndex = (totalItems - 1) / itemsPerPage;
  const int currentPageIndex = currentIndex / itemsPerPage;

  if (currentPageIndex > 0) return (currentPageIndex - 1) * itemsPerPage;
  return lastPageIndex * itemsPerPage;  // before the first page, round to the last
}

}  // namespace list_index
