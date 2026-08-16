#pragma once

#include <HalGPIO.h>

#include "util/InputArmingPolicy.h"

class GfxRenderer;

class MappedInputManager {
 public:
  enum class Button {
    Back,
    Confirm,
    Left,
    Right,
    Up,
    Down,
    Power,
    PageBack,
    PageForward,
    NavNext,
    NavPrevious,
    ScreenLeft,
    ScreenRight,
    ScreenUp,
    ScreenDown
  };
  enum class SwipeDir { None, Left, Right, Up, Down };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  MappedInputManager(HalGPIO& gpio, const GfxRenderer& renderer) : gpio(gpio), renderer(renderer) {}

  // Called once per main-loop pass. Also settles the arming gate below, so a screen
  // opened by a still-held press starts reading only after that press comes back up.
  void update() const {
    gpio.update();
    armingGate.update(isAnyPressed());
  }

  // True while any physical button is down. Used by the arming gate; also the honest
  // answer to "is the user still holding what opened this screen".
  bool isAnyPressed() const;

  // A screen change happened: swallow button edges until everything is released. See
  // util/InputArmingPolicy.h for why. Called by ActivityManager on every transition,
  // so no screen has to remember to do it.
  void armAfterScreenChange() const { armingGate.arm(); }
  bool inputArmed() const { return armingGate.armed(); }
  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool isPressed(Button button) const;
  bool hasTouch() const;
  bool wasScreenTapped(int& x, int& y) const;
  bool wasScreenTouchDown(int& x, int& y) const;
  bool isScreenTouchHeld(int& x, int& y) const;
  bool wasTapInRect(int x, int y, int width, int height) const;
  bool wasListItemTapped(int& index, int itemCount, int selectedIndex, int listTop, int listHeight,
                         bool hasSubtitle) const;
  bool wasListItemTouchedDown(int& index, int itemCount, int selectedIndex, int listTop, int listHeight,
                              bool hasSubtitle) const;

  // Combined touch interaction for a band of equal rows with caller-supplied
  // geometry — the shared hit-test for lists the theme helpers above do not
  // cover (custom row heights, option prompts, menus). Down = a held
  // tap-candidate is on a row (update the selection highlight); Tap = a tap
  // released on one (activate). rowHeight limits the hit to the top rowHeight
  // px of each step (0 = the full step, no gap band).
  enum class RowTouch : uint8_t { None, Down, Tap };
  RowTouch rowTouch(int& row, int top, int rowStep, int rowCount, int xStart = 0, int xEnd = INT32_MAX,
                    int rowHeight = 0) const;
  // Horizontal variant for side-by-side button pairs (confirmation prompts).
  RowTouch colTouch(int& col, int left, int colStep, int colCount, int yStart, int yEnd, int colWidth = 0) const;

  SwipeDir wasSwipe() const;
  bool wasHomeGesture() const;
  bool wasMenuGesture() const;
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  const GfxRenderer& getRenderer() const { return renderer; }
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;
  // Maps four screen-direction labels onto the two physical front-button roles
  // using the same live-orientation transform as ScreenLeft/Right/Up/Down.
  Labels mapDirectionalLabels(const char* back, const char* confirm, const char* left, const char* right,
                              const char* up, const char* down) const;
  // Returns the raw front button index that was pressed this frame (or -1 if none).
  int getPressedFrontButton() const;

  // Power-release gating for the reader's double-click detector. main.cpp owns the state and
  // sets this once per loop pass, before any consumer reads an edge.
  //
  //  - suppress: hide this pass's power release from everyone while the detector waits to
  //    see whether a second click is coming. Without it the release would still reach
  //    main's FORCE_REFRESH check and the reader's page-turn / footnote handlers, and the
  //    delay would buy nothing.
  //  - inject: manufacture a release on the pass the detector rules the click Single. Every
  //    downstream consumer then sees exactly the edge it would have seen without the
  //    feature, only ~280 ms later, so none of them needs to know the detector exists.
  //
  // Only Button::Power is affected, and only until the next call — main.cpp clears it every
  // pass, so a stale override cannot outlive the loop iteration that set it.
  void setPowerReleaseOverride(const bool suppress, const bool inject) {
    powerReleaseSuppressed = suppress;
    powerReleaseInjected = inject;
  }

  // True when the control axis is flipped relative to the physical buttons: the user opted into
  // orientation-following front buttons AND the screen is *currently rendered* rotated (INVERTED /
  // LANDSCAPE_CCW). Keyed on the live renderer orientation rather than the persisted reader setting,
  // so portrait UI (home, settings) never swaps while the reader and its menus do.
  [[nodiscard]] bool isNavDirectionSwapped() const;

 private:
  HalGPIO& gpio;
  // Logical-to-physical button mapping depends on what the user is actually looking at: when the
  // screen is rendered rotated, the directional buttons must flip to match. The renderer is the only
  // authority on the *live* orientation (the reader rotates it and restores portrait on exit), so we
  // read it here instead of CrossPointSettings.orientation, which is just the persisted reader
  // preference and stays "rotated" even while portrait UI like home/settings is on screen.
  const GfxRenderer& renderer;

  Button mapScreenDirection(Button button) const;
  Labels mapFrontLabels(const char* back, const char* confirm, const char* left, const char* right) const;
  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;
  bool wasBackGesture() const;
  // Fetch the pending swipe (if any) and map both endpoints to logical screen coords
  bool decodeSwipe(int& sx, int& sy, int& ex, int& ey) const;
  bool listItemFromPoint(int x, int y, int& index, int itemCount, int selectedIndex, int listTop, int listHeight,
                         bool hasSubtitle) const;
  void rememberTouchHeldTime() const;

  // Mutable because the whole read API is const and the gate is bookkeeping, not state
  // the caller can see: it only ever suppresses edges.
  mutable input_arming::Gate armingGate;
  bool powerReleaseSuppressed = false;
  bool powerReleaseInjected = false;

  mutable bool touchHeldOverrideValid = false;
  mutable unsigned long touchHeldOverrideMs = 0;
  mutable unsigned long touchHeldOverrideAt = 0;
};
