#pragma once

#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"

namespace ReaderUtils {

constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long GO_BACK_OR_HOME_MS = GO_HOME_MS;
constexpr unsigned long SKIP_HOLD_MS = 700;
constexpr unsigned long BOOKMARK_HOLD_MS = 400;

// How long a single Confirm click waits to see whether a second click follows.
// Only spent when a double-click function is bound; with the binding Disabled the
// first click still opens the reader menu immediately.
constexpr unsigned long DOUBLE_CLICK_MS = 300;
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

struct PageTurnResult {
  bool prev;
  bool next;
};

inline PageTurnResult detectPageTurn(const MappedInputManager& input) {
  const bool usePress = SETTINGS.longPressButtonBehavior == SETTINGS.OFF;
  const bool swapFront = input.isNavDirectionSwapped();
  const auto prevButton = swapFront ? MappedInputManager::Button::Right : MappedInputManager::Button::Left;
  const auto nextButton = swapFront ? MappedInputManager::Button::Left : MappedInputManager::Button::Right;
  const bool prev =
      (usePress ? (input.wasPressed(MappedInputManager::Button::PageBack) || input.wasPressed(prevButton))
                : (input.wasReleased(MappedInputManager::Button::PageBack) || input.wasReleased(prevButton)));
  const bool powerTurn = SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
                         input.wasReleased(MappedInputManager::Button::Power);
  const bool next =
      usePress
          ? (input.wasPressed(MappedInputManager::Button::PageForward) || powerTurn || input.wasPressed(nextButton))
          : (input.wasReleased(MappedInputManager::Button::PageForward) || powerTurn || input.wasReleased(nextButton));
  return {prev, next};
}

// One helper, blocking or deferred: the async form starts the refresh and
// returns so the caller can overlap CPU work with the panel's refresh time.
// Async callers must not touch the framebuffer until
// renderer.waitRefreshComplete() and must rebuild the differential baseline
// before the next page turn (the tiled grayscale cleanup does).
inline void displayWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh, bool async = false) {
  // Refresh Frequency = Never reports 0. Honour it here rather than at the counter,
  // because several callers hard-set pagesUntilFullRefresh to 1 to force a cleanup
  // pass (after an INDEXING popup, after an image page); the setting has to win over
  // those too, or "Never" would still flash.
  const int cycle = SETTINGS.getRefreshFrequency();
  const bool never = cycle == 0;
  const auto mode = (!never && pagesUntilFullRefresh <= 1) ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
  if (async) {
    renderer.displayBufferAsync(mode);
  } else {
    renderer.displayBuffer(mode);
  }
  if (never) {
    return;
  }
  if (pagesUntilFullRefresh <= 1) {
    pagesUntilFullRefresh = cycle;
  } else {
    pagesUntilFullRefresh--;
  }
}

// Grayscale anti-aliasing pass. Renders content twice (LSB + MSB) to build
// the grayscale buffer. Only the content callback is re-rendered — status bars
// and other overlays should be drawn before calling this.
// Kept as a template to avoid std::function overhead; instantiated once per reader type.
template <typename RenderFn>
void renderAntiAliased(GfxRenderer& renderer, RenderFn&& renderFn) {
  if (!renderer.storeBwBuffer()) {
    LOG_ERR("READER", "Failed to store BW buffer for anti-aliasing");
    return;
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
}

struct BackNavCallback {
  void* ctx;
  void (*fn)(void*);
};

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
// - default: go home
// - with backShortToFileBrowser: go to file browser.
// backLatch must already have observed this frame's press (see ButtonPressLatch): both
// branches below are ignored for a press this activity never saw.
inline bool handleBackNavigation(const MappedInputManager& mappedInput, ActivityManager& activityManager,
                                 const char* filePath, BackNavCallback goHome, ButtonPressLatch& backLatch) {
  if (!backLatch.seen) {
    // Stray release left over from a child screen that closed on press: swallow it so it
    // cannot be replayed later, and stay in the book.
    backLatch.release(mappedInput.wasReleased(MappedInputManager::Button::Back));
    return false;
  }
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= GO_BACK_OR_HOME_MS) {
    if (SETTINGS.backShortToFileBrowser) {
      goHome.fn(goHome.ctx);
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
      goHome.fn(goHome.ctx);
    }
    return true;
  }
  return false;
}

}  // namespace ReaderUtils
