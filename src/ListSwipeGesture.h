#pragma once

#include <cstdint>
#include <cstdlib>

// Vertical swipe scrolling for list screens. Pure geometry so the bands and the
// direction rules are host-testable; MappedInputManager binds it to live touch.
//
// The top 14% is the light panel's pull-down band and stays reserved: a scroll that
// started there would fire two things at once. The bottom 7% (HOME_EDGE_FRAC_Y) is
// Home outside books, and only for the upward swipe, so a swipe up from the last rows
// of a list still scrolls it; inside books, the bottom 14% (EDGE_BAND_FRAC_Y) opens
// the reader menu. Everything else scrolls, in the natural direction — content follows
// the finger, so a swipe up moves further down the list.
namespace list_swipe {

enum class Scroll : uint8_t { None, PageUp, PageDown };

// The top band matches the pull-down in MappedInputManager.cpp, which also reads the Home
// and reader-menu bottom edges from here.
constexpr float EDGE_BAND_FRAC_Y = 0.14f;
constexpr float HOME_EDGE_FRAC_Y = 0.07f;
// Shorter drags are taps with a wobble, not swipes.
constexpr float MIN_TRAVEL_FRAC_Y = 0.05f;

inline Scroll scrollFrom(int width, int height, int sx, int sy, int ex, int ey) {
  (void)width;
  if (height <= 0) return Scroll::None;

  const int dy = ey - sy;
  if (sy <= static_cast<int>(height * EDGE_BAND_FRAC_Y)) return Scroll::None;
  if (dy < 0 && sy >= height - static_cast<int>(height * HOME_EDGE_FRAC_Y)) return Scroll::None;

  const int dx = ex - sx;
  if (std::abs(dy) <= std::abs(dx)) return Scroll::None;
  if (std::abs(dy) < static_cast<int>(height * MIN_TRAVEL_FRAC_Y)) return Scroll::None;

  return dy < 0 ? Scroll::PageDown : Scroll::PageUp;
}

// True when an upward drag or flick starts inside the bottom `edgeFrac` of the
// screen, travels upward at least `minTravelPx`, and is more vertical than
// horizontal. Used for bottom-edge Home (7%, outside books) and the reader menu
// (14%, inside books).
inline bool isBottomEdgeUpSwipe(int height, int sx, int sy, int ex, int ey, float edgeFrac, int minTravelPx = 1) {
  if (height <= 0) return false;
  const int bottomEdgeTop = height - static_cast<int>(height * edgeFrac);
  const int travel = sy - ey;
  return sy >= bottomEdgeTop && travel >= minTravelPx && travel > std::abs(ex - sx);
}

}  // namespace list_swipe
