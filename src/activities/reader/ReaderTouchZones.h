#pragma once

#include <cstdint>

// Where a tap on the open page lands. Pure geometry over the reading surface, so
// the zone rules are testable on the host without a renderer or an input manager;
// ReaderUtils binds them to the live screen size, settings and touch state.
//
// The layout follows CrossPoint: pages turn from the outer horizontal thirds, and
// the reader menu opens from the centre third of BOTH axes, so the two never
// overlap. In swipe mode the outer thirds go quiet and swipes carry the page turns.
namespace reader_touch {

// SETTINGS.touchReaderControls.
enum class Mode : uint8_t { Off = 0, Tap = 1, Swipe = 2, InvertedTap = 3 };

// SETTINGS.showReaderMenu.
enum class MenuMode : uint8_t { Off = 0, Tap = 1, SwipeUp = 2 };

enum class TapAction : uint8_t { None, Prev, Next, Menu };

// Swipes turn pages in swipe mode only. Everywhere else a horizontal swipe stays
// free for the activities that read SwipeDir themselves.
inline bool swipeTurnsPages(Mode mode) { return mode == Mode::Swipe; }

inline TapAction tapAction(Mode mode, MenuMode menu, int width, int height, int x, int y) {
  if (mode == Mode::Off || width <= 0 || height <= 0) return TapAction::None;

  const int zoneWidth = width / 3;
  if (mode == Mode::Tap || mode == Mode::InvertedTap) {
    // Outer thirds first: a page turn is never swallowed by the menu zone.
    const bool inverted = mode == Mode::InvertedTap;
    if (x < zoneWidth) return inverted ? TapAction::Next : TapAction::Prev;
    if (x >= width - zoneWidth) return inverted ? TapAction::Prev : TapAction::Next;
  }

  if (menu != MenuMode::Tap) return TapAction::None;
  const int zoneHeight = height / 3;
  const bool inCentre = x >= zoneWidth && x < width - zoneWidth && y >= zoneHeight && y < height - zoneHeight;
  return inCentre ? TapAction::Menu : TapAction::None;
}

}  // namespace reader_touch
