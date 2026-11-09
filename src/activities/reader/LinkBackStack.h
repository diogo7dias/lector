#pragma once

#include <algorithm>

// The Link stack, free of reader state so the host tests can drive it. entries[depth-1] is
// where the newest followed link started, and Back returns there.
namespace link_stack {

struct Position {
  int spineIndex;
  int pageNumber;
};

inline bool samePage(const Position& a, const Position& b) {
  return a.spineIndex == b.spineIndex && a.pageNumber == b.pageNumber;
}

// A full stack drops its oldest entry, so Back always returns from the newest jump.
inline void push(Position* entries, int& depth, const int capacity, const Position& origin) {
  if (depth >= capacity) {
    std::copy(entries + 1, entries + capacity, entries);
    depth = capacity - 1;
  }
  entries[depth++] = origin;
}

// Landing on a page a stacked link started from is a return, typically a note's own back
// arrow: the stack is cut to where repeated Back presses would have left it.
inline int depthAfterLanding(const Position* entries, const int depth, const Position& landed) {
  for (int i = depth - 1; i >= 0; i--) {
    if (samePage(entries[i], landed)) return i;
  }
  return depth;
}

}  // namespace link_stack
