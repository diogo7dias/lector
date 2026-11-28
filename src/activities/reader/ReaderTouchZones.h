#pragma once

#include <cstdint>

// Where a tap on the open page lands, and which way a swipe turns it. Pure geometry over
// the reading surface, so the zone rules are testable on the host without a renderer or an
// input manager; ReaderUtils binds them to the live screen size, settings and touch state.
//
// Follows CrossPoint: a master switch (SETTINGS.touchReaderControls) and one gesture per
// page direction. Pages turn from the outer horizontal thirds, or from the whole page when
// only one direction accepts taps; the reader menu opens from the centre third of BOTH axes,
// so the two never overlap. A swipe-only direction leaves the taps to the menu.
namespace reader_touch {

// SETTINGS.pageTurnGesture / previousPageGesture. Values match CrossPoint.
enum class Gesture : uint8_t { TapAndSwipe = 0, TapOnly = 1, SwipeOnly = 2, InvertedTap = 3, Disabled = 4 };

// SETTINGS.showReaderMenu (centre tap). Bottom-edge swipe up always opens the reader menu inside a book.
enum class MenuMode : uint8_t { Off = 0, Tap = 1 };

// SETTINGS.tapZoneLayout. Every preset covers 100% of the reading surface with no dead zone.
enum class TapZoneLayout : uint8_t { RightHanded = 0, LeftHanded = 1, TopBottom = 2, EqualThirds = 3 };

enum class TapAction : uint8_t { None, Prev, Next, Menu };

// Top-right corner tap target on touch boards to toggle a bookmark on the current page.
// Generous enough for a thumb around the 16x22 px dog-ear without invading the reading area.
constexpr int BOOKMARK_CORNER_W = 72;
constexpr int BOOKMARK_CORNER_H = 72;

inline bool isBookmarkCornerTap(bool on, int width, int height, int x, int y) {
  if (!on || width <= 0 || height <= 0) return false;
  return x >= width - BOOKMARK_CORNER_W && x < width && y >= 0 && y < BOOKMARK_CORNER_H;
}

inline bool allowsSwipe(Gesture g) { return g == Gesture::TapAndSwipe || g == Gesture::SwipeOnly; }
inline bool allowsTap(Gesture g) {
  return g == Gesture::TapAndSwipe || g == Gesture::TapOnly || g == Gesture::InvertedTap;
}

// The page a horizontal swipe turns to: a left swipe moves on, a right swipe goes back, and a
// right-to-left book reads the other way. Each direction answers only to its own gesture.
inline TapAction swipeAction(bool on, Gesture next, Gesture prev, bool rtl, bool swipedLeft) {
  if (!on) return TapAction::None;
  const bool toNext = swipedLeft != rtl;
  if (toNext) return allowsSwipe(next) ? TapAction::Next : TapAction::None;
  return allowsSwipe(prev) ? TapAction::Prev : TapAction::None;
}

inline TapAction tapAction(bool on, Gesture next, Gesture prev, MenuMode menu, bool rtl, int width, int height, int x,
                           int y, TapZoneLayout layout = TapZoneLayout::RightHanded) {
  if (!on || width <= 0 || height <= 0) return TapAction::None;

  const int zoneWidth = width / 3;
  const int zoneHeight = height / 3;
  if (menu == MenuMode::Tap && x >= zoneWidth && x < width - zoneWidth && y >= zoneHeight && y < height - zoneHeight) {
    return TapAction::Menu;
  }

  const bool nextTaps = allowsTap(next);
  const bool prevTaps = allowsTap(prev);
  if (!nextTaps && !prevTaps) return TapAction::None;
  // Either direction on Inverted Tap swaps the shared zones; a right-to-left book swaps them too.
  const bool inverted = (next == Gesture::InvertedTap || prev == Gesture::InvertedTap) != rtl;
  // Only one direction accepts taps: it gets the whole page outside the menu zone.
  if (nextTaps != prevTaps) return nextTaps ? TapAction::Next : TapAction::Prev;

  const TapAction back = inverted ? TapAction::Next : TapAction::Prev;
  const TapAction forward = inverted ? TapAction::Prev : TapAction::Next;

  switch (layout) {
    case TapZoneLayout::RightHanded:
      return x < width / 4 ? back : forward;
    case TapZoneLayout::LeftHanded:
      return x >= width - width / 4 ? back : forward;
    case TapZoneLayout::TopBottom:
      return y < height / 2 ? back : forward;
    case TapZoneLayout::EqualThirds:
      if (x < zoneWidth) return back;
      if (x >= width - zoneWidth) return forward;
      if (menu == MenuMode::Tap) return TapAction::Menu;
      return x < width / 2 ? back : forward;
  }
  return x < width / 4 ? back : forward;
}

// True for a BCP 47 tag of a right-to-left language the reader supports: Hebrew (he, or the
// old iw), Arabic and Persian. Looks at the primary subtag only, so "ar-EG" and "he_IL" count.
inline bool isRtlLanguage(const char* tag) {
  if (tag == nullptr || tag[0] == '\0' || tag[1] == '\0') return false;
  if (tag[2] != '\0' && tag[2] != '-' && tag[2] != '_') return false;
  const char a = static_cast<char>(tag[0] | 0x20);
  const char b = static_cast<char>(tag[1] | 0x20);
  return (a == 'h' && b == 'e') || (a == 'i' && b == 'w') || (a == 'a' && b == 'r') || (a == 'f' && b == 'a');
}

// A settings file from before the split held one combined mode under "touchReaderControls":
// 0 Off, 1 Tap, 2 Swipe, 3 Inverted Tap. It becomes the switch plus one gesture for both
// directions. Anything else is not an old value and migrates to nothing.
struct Legacy {
  bool valid;
  uint8_t on;
  uint8_t gesture;
};
inline Legacy migrateLegacyMode(uint8_t mode) {
  switch (mode) {
    case 0:
      return {true, 0, static_cast<uint8_t>(Gesture::SwipeOnly)};
    case 1:
      return {true, 1, static_cast<uint8_t>(Gesture::TapOnly)};
    case 2:
      return {true, 1, static_cast<uint8_t>(Gesture::SwipeOnly)};
    case 3:
      return {true, 1, static_cast<uint8_t>(Gesture::InvertedTap)};
    default:
      return {false, 1, static_cast<uint8_t>(Gesture::SwipeOnly)};
  }
}

}  // namespace reader_touch
