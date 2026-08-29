#include "MappedInputManager.h"

#include <GfxRenderer.h>
#include <PerfLog.h>

#include <algorithm>
#include <cstdlib>

#include "CrossPointSettings.h"
#include "components/HintBandGeometry.h"
#include "components/RowHitTest.h"
#include "components/UITheme.h"
#include "util/DebugTrace.h"

bool MappedInputManager::isNavDirectionSwapped() const {
  // Key the swap on the orientation the screen is *actually* rendered at, not the persisted reader
  // setting. The reader (and its modal menus) render rotated, so navigation/labels flip there; the
  // home and settings UI render in portrait, so they never flip even when a rotated reader is configured.
  const auto orientation = renderer.getOrientation();
  return SETTINGS.frontButtonFollowOrientation &&
         (orientation == GfxRenderer::PortraitInverted || orientation == GfxRenderer::LandscapeCounterClockwise);
}

MappedInputManager::Button MappedInputManager::mapScreenDirection(const Button button) const {
  // Rows follow GfxRenderer::Orientation's declared order.
  static constexpr Button directions[][4] = {
      {Button::Left, Button::Right, Button::Up, Button::Down},
      {Button::Down, Button::Up, Button::Left, Button::Right},
      {Button::Right, Button::Left, Button::Down, Button::Up},
      {Button::Up, Button::Down, Button::Right, Button::Left},
  };

  uint8_t direction = 0;
  switch (button) {
    case Button::ScreenLeft:
      direction = 0;
      break;
    case Button::ScreenRight:
      direction = 1;
      break;
    case Button::ScreenUp:
      direction = 2;
      break;
    case Button::ScreenDown:
      direction = 3;
      break;
    default:
      return button;
  }

  const uint8_t orientation =
      SETTINGS.frontButtonFollowOrientation ? static_cast<uint8_t>(renderer.getOrientation()) : 0;
  return directions[orientation][direction];
}

bool MappedInputManager::mapButton(const Button button, bool (HalGPIO::*fn)(uint8_t) const) const {
  const auto sideLayout = SETTINGS.sideButtonLayout;
  // A tap on the hint band stands in for the front button that hint belongs to. Routed
  // through the same hardware ids the physical keys use, so every logical role — Back,
  // Confirm, NavNext, the lot — inherits it without a second mapping to keep in step.
  // Only the edge queries take it: isPressed() asks whether a key is being held, which a
  // tap never is.
  const bool tapCounts = (fn == &HalGPIO::wasPressed || fn == &HalGPIO::wasReleased);
  const int tapped = tapCounts ? tappedHintHardware() : -1;
  const bool heldQuery = (fn == &HalGPIO::isPressed);
  const bool releaseQuery = (fn == &HalGPIO::wasReleased);
  const bool pressQuery = (fn == &HalGPIO::wasPressed);
  const auto press = [&](const uint8_t hw) {
    // Router gating, before the hardware is read: every logical role that maps to this
    // key inherits it, which is the same reason the hint tap below lives here.
    const int slot = sideKeySlot(hw);
    if (slot >= 0) {
      const SideKeyOverride& override = sideKeyOverrides[slot];
      if (releaseQuery && override.injectRelease) return true;
      if (pressQuery && override.injectPress) return true;
      if (heldQuery ? override.suppressHeld : override.suppressEdges) return false;
    }
    if ((gpio.*fn)(hw)) return true;
    if (tapped < 0 || hw != tapped) return false;
    // Spend the tap here. Without this a single tap answers both wasPressed() and
    // wasReleased() in the same frame, which a physical key never does — it presses on one
    // frame and releases on a later one — and an activity that watches both would act twice.
    hintTapUsed = true;
    return true;
  };

  switch (button) {
    case Button::Back:
      // Logical Back maps to user-configured front button.
      return press(SETTINGS.frontButtonBack);
    case Button::Confirm:
      // Logical Confirm maps to user-configured front button.
      return press(SETTINGS.frontButtonConfirm);
    case Button::Left:
      // Logical Left maps to user-configured front button.
      return press(SETTINGS.frontButtonLeft);
    case Button::Right:
      // Logical Right maps to user-configured front button.
      return press(SETTINGS.frontButtonRight);
    case Button::Up:
      // Side buttons remain fixed for Up/Down.
      return press(HalGPIO::BTN_UP);
    case Button::Down:
      // Side buttons remain fixed for Up/Down.
      return press(HalGPIO::BTN_DOWN);
    case Button::Power:
      // Power button bypasses remapping.
      return press(HalGPIO::BTN_POWER);
    case Button::PageBack:
      // Reader page navigation uses side buttons and can be swapped via settings.
      switch (sideLayout) {
        case CrossPointSettings::PREV_NEXT:
          return press(HalGPIO::BTN_UP);
        case CrossPointSettings::NEXT_PREV:
          return press(HalGPIO::BTN_DOWN);
        case CrossPointSettings::SIDE_BUTTONS_DISABLED:
        default:
          return false;
      }
    case Button::PageForward:
      // Reader page navigation uses side buttons and can be swapped via settings.
      switch (sideLayout) {
        case CrossPointSettings::PREV_NEXT:
          return press(HalGPIO::BTN_DOWN);
        case CrossPointSettings::NEXT_PREV:
          return press(HalGPIO::BTN_UP);
        case CrossPointSettings::SIDE_BUTTONS_DISABLED:
        default:
          return false;
      }
    case Button::NavNext:
      // Logical "next item" navigation: side Down + front Right, with the control axis flipped in
      // INVERTED / LANDSCAPE_CCW (frontButtonFollowOrientation) so it matches the rotated hint labels.
      return isNavDirectionSwapped() ? (mapButton(Button::Up, fn) || mapButton(Button::Left, fn))
                                     : (mapButton(Button::Down, fn) || mapButton(Button::Right, fn));
    case Button::NavPrevious:
      // Logical "previous item" navigation: side Up + front Left, axis-flipped in the same orientations.
      return isNavDirectionSwapped() ? (mapButton(Button::Down, fn) || mapButton(Button::Right, fn))
                                     : (mapButton(Button::Up, fn) || mapButton(Button::Left, fn));
    case Button::ScreenLeft:
    case Button::ScreenRight:
    case Button::ScreenUp:
    case Button::ScreenDown:
      return mapButton(mapScreenDirection(button), fn);
  }

  return false;
}

namespace {
constexpr float LEFT_EDGE_BACK_GESTURE_FRAC_X = 0.25f;
constexpr float BOTTOM_EDGE_BACK_GESTURE_FRAC_Y = 0.14f;
constexpr float TOP_EDGE_MENU_GESTURE_FRAC_Y = 0.14f;
// How far down the finger must travel before a top-edge drag counts as the gesture.
// A fifth of the screen: past any accidental slip, short of a full page-height drag.
constexpr float MENU_DRAG_TRAVEL_FRAC_Y = 0.20f;
constexpr unsigned long TOUCH_DOWN_SELECT_DELAY_MS = 90;
constexpr unsigned long TOUCH_HELD_OVERRIDE_WINDOW_MS = 250;
}  // namespace

bool MappedInputManager::hasTouch() const { return gpio.hasTouch(); }

int MappedInputManager::tappedHintHardware() const {
  // Slots are painted left to right in hardware order, the same order mapFrontLabels()
  // fills its labels in, so slot N belongs to front button N.
  static constexpr uint8_t kSlotHardware[hint_band::kSlotCount] = {HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM,
                                                                   HalGPIO::BTN_LEFT, HalGPIO::BTN_RIGHT};
  const hint_band::Painted& painted = hint_band::lastPainted();

  float nx = 0.0f;
  float ny = 0.0f;
  // Read the tap without consuming it at the HAL: an activity that also handles taps of
  // its own still sees this one, and a tap outside the band is left entirely alone.
  if (!gpio.wasTouchTap(nx, ny)) {
    hintTapUsed = false;  // the tap event is over; the next one starts unspent
    return -1;
  }
  if (hintTapUsed || !painted.valid) return -1;
  int x = 0;
  int y = 0;
  renderer.tapToLogical(nx, ny, x, y);

  const int slot = hint_band::tappedSlot(painted.band, x, y, painted.labelled);
  if (slot < 0) return -1;
  rememberTouchHeldTime();
  return kSlotHardware[slot];
}

bool MappedInputManager::wasRowTapped(int& item) const {
  if (!gpio.hasTouch()) return false;
  int x = 0;
  int y = 0;
  if (!wasScreenTapped(x, y)) return false;
  // A tap that lands in the hint band belongs to that button, not to a row, even when a
  // list's rect runs under the band. Tested on geometry alone, so it holds whether or not
  // the band's own press has already been spent this frame.
  const hint_band::Painted& painted = hint_band::lastPainted();
  if (painted.valid && hint_band::tappedSlot(painted.band, x, y, painted.labelled) >= 0) return false;
  const int hit = row_hit::lastRows().itemAt(x, y);
  if (hit == row_hit::kNoItem) return false;
  item = hit;
  return true;
}

void MappedInputManager::rememberTouchHeldTime() const {
  touchHeldOverrideValid = true;
  touchHeldOverrideMs = gpio.lastTouchHeldMs();
  touchHeldOverrideAt = millis();
}

bool MappedInputManager::wasScreenTapped(int& x, int& y) const {
  float nx = 0.0f;
  float ny = 0.0f;
  if (!gpio.wasTouchTap(nx, ny)) return false;
  renderer.tapToLogical(nx, ny, x, y);
  rememberTouchHeldTime();
  return true;
}

bool MappedInputManager::wasScreenTouchDown(int& x, int& y) const {
  float nx = 0.0f;
  float ny = 0.0f;
  unsigned long heldMs = 0;
  if (!gpio.isTouchTapCandidate(nx, ny, heldMs)) return false;
  if (heldMs < TOUCH_DOWN_SELECT_DELAY_MS) return false;
  renderer.tapToLogical(nx, ny, x, y);
  return true;
}

bool MappedInputManager::takeScreenTouchDown(int& x, int& y) {
  if (!wasScreenTouchDown(x, y)) return false;
  spendTouchContact();
  return true;
}

// The SDK's own remedy for a contact that has already been acted on: it drops the rest of
// this contact, so neither a later pass nor the lift can act again.
void MappedInputManager::spendTouchContact() { gpio.suppressTouchContact(); }

bool MappedInputManager::isScreenTouchHeld(int& x, int& y) const {
  // Live contact position while the finger is down (no tap-slop gate) — drag tracking.
  float nx = 0.0f;
  float ny = 0.0f;
  if (!gpio.isTouchHeldAt(nx, ny)) return false;
  renderer.tapToLogical(nx, ny, x, y);
  return true;
}

bool MappedInputManager::wasTapInRect(const int x, const int y, const int width, const int height) const {
  int tx = 0;
  int ty = 0;
  return wasScreenTapped(tx, ty) && tx >= x && tx < x + width && ty >= y && ty < y + height;
}

bool MappedInputManager::listItemFromPoint(const int x, const int y, int& index, const int itemCount,
                                           const int selectedIndex, const int listTop, const int listHeight,
                                           const bool hasSubtitle) const {
  (void)x;
  if (itemCount <= 0) return false;
  if (y < listTop || y >= listTop + listHeight) return false;

  const auto& theme = UITheme::getInstance().getTheme();
  const int rowStep = theme.getListRowStep(hasSubtitle);
  if (rowStep <= 0) return false;

  const int pageItems = theme.getListPageItems(listHeight, hasSubtitle);
  if (pageItems <= 0) return false;
  const int pageStart = std::max(0, selectedIndex / pageItems) * pageItems;
  const int row = (y - listTop) / rowStep;
  const int tapped = pageStart + row;
  if (row < 0 || row >= pageItems || tapped >= itemCount) return false;
  index = tapped;
  return true;
}

bool MappedInputManager::wasListItemTapped(int& index, const int itemCount, const int selectedIndex, const int listTop,
                                           const int listHeight, const bool hasSubtitle) const {
  int tx = 0;
  int ty = 0;
  return wasScreenTapped(tx, ty) &&
         listItemFromPoint(tx, ty, index, itemCount, selectedIndex, listTop, listHeight, hasSubtitle);
}

bool MappedInputManager::wasListItemTouchedDown(int& index, const int itemCount, const int selectedIndex,
                                                const int listTop, const int listHeight, const bool hasSubtitle) const {
  int tx = 0;
  int ty = 0;
  return wasScreenTouchDown(tx, ty) &&
         listItemFromPoint(tx, ty, index, itemCount, selectedIndex, listTop, listHeight, hasSubtitle);
}

MappedInputManager::RowTouch MappedInputManager::rowTouch(int& row, const int top, const int rowStep,
                                                          const int rowCount, const int xStart, const int xEnd,
                                                          const int rowHeight) const {
  if (rowStep <= 0 || rowCount <= 0) return RowTouch::None;
  const auto hit = [&](const int x, const int y) {
    if (x < xStart || x >= xEnd || y < top) return false;
    const int r = (y - top) / rowStep;
    if (r >= rowCount) return false;
    if (rowHeight > 0 && (y - top) % rowStep >= rowHeight) return false;
    row = r;
    return true;
  };
  int x = 0;
  int y = 0;
  if (wasScreenTouchDown(x, y) && hit(x, y)) return RowTouch::Down;
  if (wasScreenTapped(x, y) && hit(x, y)) return RowTouch::Tap;
  return RowTouch::None;
}

MappedInputManager::RowTouch MappedInputManager::colTouch(int& col, const int left, const int colStep,
                                                          const int colCount, const int yStart, const int yEnd,
                                                          const int colWidth) const {
  if (colStep <= 0 || colCount <= 0) return RowTouch::None;
  const auto hit = [&](const int x, const int y) {
    if (y < yStart || y >= yEnd || x < left) return false;
    const int c = (x - left) / colStep;
    if (c >= colCount) return false;
    if (colWidth > 0 && (x - left) % colStep >= colWidth) return false;
    col = c;
    return true;
  };
  int x = 0;
  int y = 0;
  if (wasScreenTouchDown(x, y) && hit(x, y)) return RowTouch::Down;
  if (wasScreenTapped(x, y) && hit(x, y)) return RowTouch::Tap;
  return RowTouch::None;
}

bool MappedInputManager::decodeSwipe(int& sx, int& sy, int& ex, int& ey) const {
  float nxs = 0.0f;
  float nys = 0.0f;
  float nxe = 0.0f;
  float nye = 0.0f;
  if (!gpio.wasSwipe(nxs, nys, nxe, nye)) return false;
  renderer.tapToLogical(nxs, nys, sx, sy);
  renderer.tapToLogical(nxe, nye, ex, ey);
  return true;
}

MappedInputManager::SwipeDir MappedInputManager::wasSwipe() const {
  int sx = 0;
  int sy = 0;
  int ex = 0;
  int ey = 0;
  if (!decodeSwipe(sx, sy, ex, ey)) return SwipeDir::None;
  const int dx = ex - sx;
  const int dy = ey - sy;
  if (std::abs(dx) >= std::abs(dy)) {
    return dx < 0 ? SwipeDir::Left : SwipeDir::Right;
  }
  return dy < 0 ? SwipeDir::Up : SwipeDir::Down;
}

bool MappedInputManager::wasBackGesture() const {
  // Back = left-to-right swipe starting near the left edge. Edge-anchored so that
  // mid-screen horizontal swipes stay available to activities that consume
  // SwipeDir::Left/Right (e.g. percent selection, image viewer).
  int sx = 0;
  int sy = 0;
  int ex = 0;
  int ey = 0;
  if (!decodeSwipe(sx, sy, ex, ey)) return false;
  const bool hit = sx <= renderer.getScreenWidth() * LEFT_EDGE_BACK_GESTURE_FRAC_X && ex > sx &&
                   std::abs(ex - sx) > std::abs(ey - sy);
  if (hit) rememberTouchHeldTime();
  return hit;
}

bool MappedInputManager::wasMenuGesture() const {
  // Two ways in, because one was not enough.
  //
  // The first is a pull-down tracked here, pass by pass: the finger goes down inside the
  // top band and is dragged more than a fifth of the screen straight down. No time limit,
  // so a slow, deliberate pull works — which is how a panel dragged off the top edge is
  // actually handled, and it is the reason this gesture was all but unusable before: the
  // SDK's swipe is a flick, under 700 ms, and a deliberate drag rarely beats that clock.
  //
  // The second is that flick, kept because a fast swipe from the edge never lingers long
  // enough for the drag to arm.
  const int topEdgeBottom = static_cast<int>(renderer.getScreenHeight() * TOP_EDGE_MENU_GESTURE_FRAC_Y);
  int tx = 0;
  int ty = 0;
  if (isScreenTouchHeld(tx, ty)) {
    if (!menuDragTracking_) {
      // First sighting of this contact. Deliberately NOT wasScreenTouchDown(): that asks
      // whether the contact is still a tap candidate, which a finger already sliding down
      // the screen has stopped being, so the drag could never arm from it.
      menuDragTracking_ = true;
      menuDragFired_ = false;
      menuDragStartX_ = tx;
      menuDragStartY_ = ty;
      debug_trace::note("touch down (%d,%d) topEdge=%d", tx, ty, topEdgeBottom);
    }
    const int travel = ty - menuDragStartY_;
    if (!menuDragFired_ && menuDragStartY_ <= topEdgeBottom &&
        travel >= static_cast<int>(renderer.getScreenHeight() * MENU_DRAG_TRAVEL_FRAC_Y) &&
        travel > std::abs(tx - menuDragStartX_)) {
      menuDragFired_ = true;
      // The rest of the contact belongs to the gesture: without this the finger lifting
      // would tap whatever the panel just drew under it.
      gpio.suppressTouchContact();
      debug_trace::note("top-edge drag fired: %d px down from y=%d", travel, menuDragStartY_);
      rememberTouchHeldTime();
      return true;
    }
  } else if (menuDragTracking_) {
    menuDragTracking_ = false;
    debug_trace::note("contact ended, started (%d,%d), drag did not fire", menuDragStartX_, menuDragStartY_);
  }

  // Downward swipe starting at the top edge (mirror of the bottom-edge home gesture).
  int sx = 0;
  int sy = 0;
  int ex = 0;
  int ey = 0;
  if (!decodeSwipe(sx, sy, ex, ey)) {
    // A contact that ended without qualifying as a swipe at all: too slow (over 700 ms),
    // or under the 60 px the flick needs. Logged because the two failures look identical
    // on the device, and only the log can tell them apart.
    if (gpio.wasTouchReleased()) debug_trace::note("touch released, no flick decoded");
    return false;
  }
  const bool hit = sy <= topEdgeBottom && ey > sy && std::abs(ey - sy) > std::abs(ex - sx);
  debug_trace::note("flick (%d,%d)->(%d,%d) topEdge=%d menu=%d", sx, sy, ex, ey, topEdgeBottom, hit ? 1 : 0);
  if (hit) rememberTouchHeldTime();
  return hit;
}

bool MappedInputManager::wasBottomEdgeUpSwipe() const {
  int sx = 0;
  int sy = 0;
  int ex = 0;
  int ey = 0;
  if (decodeSwipe(sx, sy, ex, ey)) {
    const int bottomEdgeTop =
        renderer.getScreenHeight() - static_cast<int>(renderer.getScreenHeight() * BOTTOM_EDGE_BACK_GESTURE_FRAC_Y);
    if (sy >= bottomEdgeTop && ey < sy && std::abs(ey - sy) > std::abs(ex - sx)) {
      rememberTouchHeldTime();
      return true;
    }
  }
  return false;
}

bool MappedInputManager::wasHomeGesture() const {
  // On a board with a capacitive Home key that key IS Home, which frees the bottom
  // edge for the reader menu (wasReaderMenuSwipeUp).
  if (gpio.hasHomeKey()) {
    // See setHomeKeyOverride(): the router holds a tap back while it decides whether a
    // second one is coming, and replays it here when it rules the tap a plain single.
    if (homeKeyInjected) return true;
    if (homeKeySuppressed) return false;
    return gpio.wasHomeKeyTapped();
  }
  return wasBottomEdgeUpSwipe();
}

int MappedInputManager::sideKeySlot(const uint8_t hardware) {
  if (hardware == HalGPIO::BTN_UP) return 0;
  if (hardware == HalGPIO::BTN_DOWN) return 1;
  return -1;
}

void MappedInputManager::setSideKeyOverride(const uint8_t hardware, const bool suppressEdges, const bool suppressHeld,
                                            const bool injectPress, const bool injectRelease) {
  const int slot = sideKeySlot(hardware);
  if (slot < 0) return;
  sideKeyOverrides[slot] = SideKeyOverride{suppressEdges, suppressHeld, injectPress, injectRelease};
}

void MappedInputManager::clearBindingOverrides() {
  for (auto& override : sideKeyOverrides) override = SideKeyOverride{};
  homeKeySuppressed = false;
  homeKeyInjected = false;
}

bool MappedInputManager::wasScreenLongPress(int& x, int& y) const {
  float nx = 0.0f;
  float ny = 0.0f;
  if (!gpio.wasTouchLongPress(nx, ny)) return false;
  // Consuming the long press implies acting on it: drop the rest of the contact so the
  // finger lift cannot also tap whatever the action opened.
  gpio.suppressTouchContact();
  renderer.tapToLogical(nx, ny, x, y);
  return true;
}

bool MappedInputManager::wasScreenTouchReleased() const { return gpio.wasTouchReleased(); }

bool MappedInputManager::wasReaderMenuSwipeUp() const { return gpio.hasHomeKey() && wasBottomEdgeUpSwipe(); }

list_swipe::Scroll MappedInputManager::wasListScrollSwipe() const {
  int sx = 0;
  int sy = 0;
  int ex = 0;
  int ey = 0;
  if (!decodeSwipe(sx, sy, ex, ey)) return list_swipe::Scroll::None;
  const auto scroll = list_swipe::scrollFrom(renderer.getScreenWidth(), renderer.getScreenHeight(), sx, sy, ex, ey);
  if (scroll != list_swipe::Scroll::None) rememberTouchHeldTime();
  return scroll;
}

bool MappedInputManager::wasPressed(const Button button) const {
  if (button == Button::Back && wasBackGesture()) return true;
  return mapButton(button, &HalGPIO::wasPressed);
}

bool MappedInputManager::wasReleased(const Button button) const {
  if (button == Button::Back && wasBackGesture()) return true;
  // See setPowerReleaseOverride(): the reader's double-click detector holds a power release
  // back for one window and then replays it here, so every consumer keeps its existing code.
  if (button == Button::Power) {
    if (powerReleaseInjected) return true;
    if (powerReleaseSuppressed) return false;
  }
  return mapButton(button, &HalGPIO::wasReleased);
}

bool MappedInputManager::isPressed(const Button button) const { return mapButton(button, &HalGPIO::isPressed); }

bool MappedInputManager::isAnyPressed() const {
  // Every physical button, not the logical ones: a logical button can map to two
  // physical ones and a screen change must wait for whichever is actually down.
  for (uint8_t i = HalGPIO::BTN_BACK; i <= HalGPIO::BTN_POWER; ++i) {
    if (gpio.isPressed(i)) return true;
  }
  return false;
}

bool MappedInputManager::wasAnyPressed() const { return gpio.wasAnyPressed(); }

bool MappedInputManager::wasAnyReleased() const { return gpio.wasAnyReleased(); }

unsigned long MappedInputManager::getHeldTime() const {
  if (!gpio.wasAnyPressed() && !gpio.wasAnyReleased() && touchHeldOverrideValid &&
      millis() - touchHeldOverrideAt <= TOUCH_HELD_OVERRIDE_WINDOW_MS) {
    return touchHeldOverrideMs;
  }
  touchHeldOverrideValid = false;
  return gpio.getHeldTime();
}

MappedInputManager::Labels MappedInputManager::mapLabels(const char* back, const char* confirm, const char* previous,
                                                         const char* next) const {
  // Swap previous/next labels to match the page turn direction swap in INVERTED and LANDSCAPE_CCW.
  const bool swapLabels = isNavDirectionSwapped();
  const char* leftLabel = swapLabels ? next : previous;
  const char* rightLabel = swapLabels ? previous : next;

  return mapFrontLabels(back, confirm, leftLabel, rightLabel);
}

MappedInputManager::Labels MappedInputManager::mapDirectionalLabels(const char* back, const char* confirm,
                                                                    const char* left, const char* right, const char* up,
                                                                    const char* down) const {
  const auto labelForButton = [&](const Button rawButton) {
    if (mapScreenDirection(Button::ScreenLeft) == rawButton) return left;
    if (mapScreenDirection(Button::ScreenRight) == rawButton) return right;
    if (mapScreenDirection(Button::ScreenUp) == rawButton) return up;
    if (mapScreenDirection(Button::ScreenDown) == rawButton) return down;
    return "";
  };
  return mapFrontLabels(back, confirm, labelForButton(Button::Left), labelForButton(Button::Right));
}

MappedInputManager::Labels MappedInputManager::mapFrontLabels(const char* back, const char* confirm, const char* left,
                                                              const char* right) const {
  // Build the label order based on the configured hardware mapping.
  auto labelForHardware = [&](uint8_t hw) -> const char* {
    // Compare against configured logical roles and return the matching label.
    if (hw == SETTINGS.frontButtonBack) {
      return back;
    }
    if (hw == SETTINGS.frontButtonConfirm) {
      return confirm;
    }
    if (hw == SETTINGS.frontButtonLeft) {
      return left;
    }
    if (hw == SETTINGS.frontButtonRight) {
      return right;
    }
    return "";
  };

  return {labelForHardware(HalGPIO::BTN_BACK), labelForHardware(HalGPIO::BTN_CONFIRM),
          labelForHardware(HalGPIO::BTN_LEFT), labelForHardware(HalGPIO::BTN_RIGHT)};
}

int MappedInputManager::getPressedFrontButton() const {
  // Scan the raw front buttons in hardware order.
  // This bypasses remapping so the remap activity can capture physical presses.
  if (gpio.wasPressed(HalGPIO::BTN_BACK)) {
    return HalGPIO::BTN_BACK;
  }
  if (gpio.wasPressed(HalGPIO::BTN_CONFIRM)) {
    return HalGPIO::BTN_CONFIRM;
  }
  if (gpio.wasPressed(HalGPIO::BTN_LEFT)) {
    return HalGPIO::BTN_LEFT;
  }
  if (gpio.wasPressed(HalGPIO::BTN_RIGHT)) {
    return HalGPIO::BTN_RIGHT;
  }
  return -1;
}
