#pragma once

#include <cstdint>
#include <cstdlib>

// Vertical swipe scrolling for list screens. Pure geometry so the bands and the
// direction rules are host-testable; MappedInputManager binds it to live touch.
//
// The top 14% is the light panel's pull-down band and stays reserved: a scroll that
// started there would fire two things at once. The bottom edge is Home's, and only for
// the upward swipe: a narrower 7%, so a swipe up from the last rows of a list still
// scrolls it. Everything else scrolls, in the natural direction — content follows the
// finger, so a swipe up moves further down the list.
namespace list_swipe {

enum class Scroll : uint8_t { None, PageUp, PageDown };

// The top band matches the pull-down in MappedInputManager.cpp, which also reads the Home
// edge from here.
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

}  // namespace list_swipe
