#pragma once

#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/reader/ReaderTouchZones.h"
#include "components/UITheme.h"

namespace ReaderUtils {

constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long GO_BACK_OR_HOME_MS = GO_HOME_MS;
constexpr unsigned long SKIP_HOLD_MS = 700;  // holding a page button this long skips pages (.xtc)
constexpr unsigned long BOOKMARK_HOLD_MS = 400;

constexpr unsigned long BOOKMARK_MESSAGE_DURATION_MS = 2500;

inline void applyOrientation(GfxRenderer& renderer, const uint8_t orientation) {
  switch (orientation) {
    case CrossPointSettings::ORIENTATION::PORTRAIT:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case CrossPointSettings::ORIENTATION::INVERTED:
      renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }
}

struct TouchPageTurn {
  bool prev;
  bool next;
  bool menu;
  unsigned long heldMs;
};

inline reader_touch::Gesture nextPageGesture() { return static_cast<reader_touch::Gesture>(SETTINGS.pageTurnGesture); }
inline reader_touch::Gesture previousPageGesture() {
  return static_cast<reader_touch::Gesture>(SETTINGS.previousPageGesture);
}
inline reader_touch::MenuMode readerMenuMode() { return static_cast<reader_touch::MenuMode>(SETTINGS.showReaderMenu); }
inline reader_touch::TapZoneLayout tapZoneLayout() {
  return static_cast<reader_touch::TapZoneLayout>(SETTINGS.tapZoneLayout);
}

// Touch on the open page: page turns and the tap into the reader menu, in one pass.
// A swipe is read first so a slow drag never also registers as a tap. A right-to-left book
// (Hebrew, Arabic, Persian) turns pages the other way, for swipes and tap zones alike.
inline TouchPageTurn detectTouchPageTurn(const GfxRenderer& renderer, const MappedInputManager& input,
                                         const bool rtlBook = false) {
  TouchPageTurn result{false, false, false, 0};
  const bool on = SETTINGS.touchReaderControls != 0;
  if (!on || !input.hasTouch()) return result;

  const auto dir = input.wasSwipe();
  if (dir == MappedInputManager::SwipeDir::Left || dir == MappedInputManager::SwipeDir::Right) {
    const auto turn = reader_touch::swipeAction(on, nextPageGesture(), previousPageGesture(), rtlBook,
                                                dir == MappedInputManager::SwipeDir::Left);
    result.next = turn == reader_touch::TapAction::Next;
    result.prev = turn == reader_touch::TapAction::Prev;
    if (result.next || result.prev) return result;
  }

  int x = 0;
  int y = 0;
  if (!input.wasScreenTapped(x, y)) return result;

  switch (reader_touch::tapAction(on, nextPageGesture(), previousPageGesture(), readerMenuMode(), rtlBook,
                                  renderer.getScreenWidth(), renderer.getScreenHeight(), x, y, tapZoneLayout())) {
    case reader_touch::TapAction::Prev:
      result.prev = true;
      break;
    case reader_touch::TapAction::Next:
      result.next = true;
      break;
    case reader_touch::TapAction::Menu:
      result.menu = true;
      break;
    case reader_touch::TapAction::None:
      break;
  }
  result.heldMs = gpio.lastTouchHeldMs();
  return result;
}

// Reader-menu gestures are independent of page-turn controls. Turning page
// controls off must not strand the menu, especially on touch boards: bottom-edge
// swipe up always opens the reader menu inside a book, and Show Reader Menu controls
// whether a centre tap does too.
inline bool isTouchMenuGesture(const GfxRenderer& renderer, const MappedInputManager& input, bool centreTap = false) {
  if (!input.hasTouch()) return false;
  if (input.wasReaderMenuSwipeUp()) return true;
  if (readerMenuMode() == reader_touch::MenuMode::Off) return false;
  if (centreTap) return true;

  // With page-turn controls off, detect the centre tap here instead of in
  // detectTouchPageTurn(), which intentionally ignores page input in that mode.
  if (SETTINGS.touchReaderControls == 0 && readerMenuMode() == reader_touch::MenuMode::Tap) {
    int x = 0;
    int y = 0;
    if (!input.wasScreenTapped(x, y)) return false;
    const int third = renderer.getScreenWidth() / 3;
    const int thirdHeight = renderer.getScreenHeight() / 3;
    return x >= third && x < renderer.getScreenWidth() - third && y >= thirdHeight &&
           y < renderer.getScreenHeight() - thirdHeight;
  }
  return false;
}

// Which way the page button held down right now points: -1 back, +1 forward, 0 none. A held
// button hidden by a bound hold (the router masks it) reads as not held, so a hold the user
// bound to an action never also skips pages.
inline int heldPageDirection(const MappedInputManager& input) {
  const bool swapFront = input.isNavDirectionSwapped();
  const auto prevButton = swapFront ? MappedInputManager::Button::Right : MappedInputManager::Button::Left;
  const auto nextButton = swapFront ? MappedInputManager::Button::Left : MappedInputManager::Button::Right;
  if (input.isPressed(MappedInputManager::Button::PageBack) || input.isPressed(prevButton)) return -1;
  if (input.isPressed(MappedInputManager::Button::PageForward) || input.isPressed(nextButton)) return 1;
  return 0;
}

struct PageTurnResult {
  bool prev;
  bool next;
};

// Always the press edge. Page turning is the one thing the device does constantly,
// so it is the one thing that must never wait: nothing is bound to holding a page
// turn button any more, which is what makes firing this early unambiguous.
//
// Power is absent: paging on the power button is a binding like any other now, dispatched
// through the reader's runBoundAction rather than read off a raw release here.
inline PageTurnResult detectPageTurn(const MappedInputManager& input) {
  const bool swapFront = input.isNavDirectionSwapped();
  const auto prevButton = swapFront ? MappedInputManager::Button::Right : MappedInputManager::Button::Left;
  const auto nextButton = swapFront ? MappedInputManager::Button::Left : MappedInputManager::Button::Right;
  const bool prev = input.wasPressed(MappedInputManager::Button::PageBack) || input.wasPressed(prevButton);
  const bool next = input.wasPressed(MappedInputManager::Button::PageForward) || input.wasPressed(nextButton);
  return {prev, next};
}

inline void displayWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh) {
  const auto mode = (pagesUntilFullRefresh <= 1) ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
  renderer.displayBuffer(mode);
  if (pagesUntilFullRefresh <= 1) {
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    pagesUntilFullRefresh--;
  }
}

// Grayscale anti-aliasing pass. Renders content twice (LSB + MSB) to build
// the grayscale buffer. Only the content callback is re-rendered — status bars
// and other overlays should be drawn before calling this.
// Kept as a template to avoid std::function overhead; instantiated once per reader type.
// False when the BW frame could not be stored: nothing was drawn, the BW page stands.
template <typename RenderFn>
bool renderAntiAliased(GfxRenderer& renderer, RenderFn&& renderFn) {
  if (!renderer.storeBwBuffer()) {
    LOG_ERR("READER", "Failed to store BW buffer for anti-aliasing");
    return false;
  }

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderFn();
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderFn();
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);

  renderer.restoreBwBuffer();
  return true;
}

// Wallpaper Hold, the same in every reader: flip the rotation pause, say which way it
// went, and put the next paint on the ghost-cleanup path so the popup does not ghost
// under the page. The caller requests the redraw.
inline void toggleWallpaperHold(const GfxRenderer& renderer, int& pagesUntilFullRefresh) {
  SETTINGS.wallpaperRotationPaused = SETTINGS.wallpaperRotationPaused ? 0 : 1;
  SETTINGS.saveToFile();
  GUI.drawPopup(renderer, SETTINGS.wallpaperRotationPaused ? tr(STR_ROTATION_PAUSED) : tr(STR_ROTATION_RESUMED));
  pagesUntilFullRefresh = 0;
}

// Pairs a button press with its release WITHIN one activity.
//
// The firmware mixes two input models: the Settings family acts on button PRESS, the
// readers and their menus act on RELEASE. So when a child screen closes on a press, the
// matching release is still to come, and it lands on whatever activity is current when the
// finger lifts — the reader underneath. The reader then read that release as its own Back
// and left the book: closing Reader Settings dropped the user on the home screen instead
// of back onto the page.
//
// The rule this enforces: a release only counts for an activity that also saw its press.
// observe() must be called once per loop pass, before any early return, so a genuine
// press is never missed.
struct ButtonPressLatch {
  bool seen = false;

  void observe(const bool pressedThisFrame) {
    if (pressedThisFrame) seen = true;
  }

  // True only when this activity saw the press that belongs to this release. Consumes
  // the pairing either way, so a stray release is swallowed instead of lingering.
  bool release(const bool releasedThisFrame) {
    if (!releasedThisFrame) return false;
    const bool paired = seen;
    seen = false;
    return paired;
  }
};

// Returns true if the back button was consumed (caller should return).
// Long press (>= GO_BACK_OR_HOME_MS):
// - default: go to file browser
// - with backShortToFileBrowser: go home
// Short press (< GO_BACK_OR_HOME_MS):
// - default: back where the book was opened (Home, the browser folder or the Library)
// - with backShortToFileBrowser: go to file browser.
// backLatch must already have observed this frame's press (see ButtonPressLatch): both
// branches below are ignored for a press this activity never saw.
inline bool handleBackNavigation(const MappedInputManager& mappedInput, ActivityManager& activityManager,
                                 const char* filePath, ButtonPressLatch& backLatch) {
  // A left-edge right swipe is page-back in the reader, never reader exit.
  if (mappedInput.wasBackGesture()) return false;
  if (!backLatch.seen) {
    // Stray release left over from a child screen that closed on press: swallow it so it
    // cannot be replayed later, and stay in the book.
    backLatch.release(mappedInput.wasReleased(MappedInputManager::Button::Back));
    return false;
  }
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= GO_BACK_OR_HOME_MS) {
    if (SETTINGS.backShortToFileBrowser) {
      activityManager.goHome();
    } else {
      activityManager.goToFileBrowser(filePath);
    }
    return true;
  }
  if (backLatch.release(mappedInput.wasReleased(MappedInputManager::Button::Back)) &&
      mappedInput.getHeldTime() < GO_BACK_OR_HOME_MS) {
    if (SETTINGS.backShortToFileBrowser) {
      activityManager.goToFileBrowser(filePath);
    } else {
      activityManager.goToReaderOrigin();
    }
    return true;
  }
  return false;
}

}  // namespace ReaderUtils
