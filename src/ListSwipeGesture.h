#pragma once

#include <cstdint>
#include <cstdlib>

// Vertical swipe scrolling for list screens. Pure geometry so the bands and the
// direction rules are host-testable; MappedInputManager binds it to live touch.
//
// The top and bottom 14% are the menu and Home gesture bands and stay reserved: a
// scroll that started there would fire two things at once. Everything between them
// scrolls, in the natural direction — content follows the finger, so a swipe up
// moves further down the list.
namespace list_swipe {

enum class Scroll : uint8_t { None, PageUp, PageDown };

// Matches the gesture bands in MappedInputManager.cpp.
constexpr float EDGE_BAND_FRAC_Y = 0.14f;
// Shorter drags are taps with a wobble, not swipes.
constexpr float MIN_TRAVEL_FRAC_Y = 0.05f;

inline Scroll scrollFrom(int width, int height, int sx, int sy, int ex, int ey) {
  (void)width;
  if (height <= 0) return Scroll::None;

  const int band = static_cast<int>(height * EDGE_BAND_FRAC_Y);
  if (sy <= band || sy >= height - band) return Scroll::None;

  const int dy = ey - sy;
  const int dx = ex - sx;
  if (std::abs(dy) <= std::abs(dx)) return Scroll::None;
  if (std::abs(dy) < static_cast<int>(height * MIN_TRAVEL_FRAC_Y)) return Scroll::None;

  return dy < 0 ? Scroll::PageDown : Scroll::PageUp;
}

}  // namespace list_swipe
