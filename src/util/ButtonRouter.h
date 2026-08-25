#pragma once

#include <cstdint>

#include "util/BoundActionScope.h"
#include "util/BoundMenuActions.h"
#include "util/ButtonGestures.h"

// What each physical key does, per gesture, in the context the reader is in.
//
// One gesture detector per key, armed from that key's three bindings: the detector only
// waits for a second press where a double is bound, and only watches the clock where a
// hold is, so a key carrying nothing but its own paging behaves exactly as it did before
// the feature existed. See util/ButtonGestures.h for why the waits are conditional.
//
// The router names the action a gesture landed on; it never runs one. The caller owns
// dispatch, because what an action does depends on what is on screen.
namespace button_router {

using bound_action::LP_MENU_DISABLED;
using bound_action::LP_MENU_PAGE_NEXT;
using bound_action::LP_MENU_PAGE_PREV;

// What a key already does when nothing is bound to it: paging for a side key, Home for
// the capacitive Home key. Binding one of these back to the same key is answered by
// replaying its raw edge rather than by dispatching, so the reader and every list keep
// their own code for it, repeat included.
struct Native {
  uint8_t first = LP_MENU_DISABLED;
  uint8_t second = LP_MENU_DISABLED;
};

// The two side keys page; the capacitive Home key goes home.
constexpr Native NATIVE_SIDE_KEY{LP_MENU_PAGE_PREV, LP_MENU_PAGE_NEXT};
constexpr Native NATIVE_HOME_KEY{bound_action::LP_MENU_GO_HOME, LP_MENU_DISABLED};

// The three bindings of one key, in gesture order.
struct Binding {
  uint8_t single = LP_MENU_DISABLED;
  uint8_t doubleClick = LP_MENU_DISABLED;
  uint8_t hold = LP_MENU_DISABLED;
};

// The outcome of feeding the router one edge or one tick.
struct Fired {
  bool valid = false;
  uint8_t function = LP_MENU_DISABLED;
  // The binding is what this key already did, so the caller replays the raw edge it was
  // holding back instead of dispatching; see Native.
  bool replayRawEdge = false;
};

// Left side key, right side key, Home. Indexes match CrossPointSettings::BOUND_BUTTON.
constexpr int KEY_COUNT = 3;

class Router {
 public:
  // Re-arms one key. Called whenever the bindings or the context change (entering a book,
  // leaving it): the same key can carry a double in one context and not the other. Any
  // gesture still pending on that key is dropped, so a click pressed on the page cannot
  // fire into the screen that replaced it.
  void configure(const int key, const Binding& binding, const Native& native = NATIVE_SIDE_KEY) {
    if (!valid(key)) return;
    bindings_[key] = binding;
    natives_[key] = native;
    detectors_[key].reset();
    detectors_[key].configure(binding.doubleClick != LP_MENU_DISABLED, binding.hold != LP_MENU_DISABLED);
    detectors_[key].setSingleBound(binding.single != LP_MENU_DISABLED);
  }

  Fired onPress(const int key, const uint32_t nowMs) {
    if (!valid(key)) return {};
    return resolve(key, detectors_[key].onPress(nowMs));
  }

  Fired onRelease(const int key, const uint32_t nowMs) {
    if (!valid(key)) return {};
    return resolve(key, detectors_[key].onRelease(nowMs));
  }

  // Called every loop pass, edge or no edge: a hold fires while the key is still down and
  // a deferred single fires when its double-click window closes.
  Fired tick(const int key, const uint32_t nowMs) {
    if (!valid(key)) return {};
    return resolve(key, detectors_[key].tick(nowMs));
  }

  // Names this key's hold outright, without timing it. The capacitive Home key reports a
  // completed long press rather than a held edge, so there is nothing here to time. Drops
  // any gesture still in flight on that key: the same press may also be reported as a tap,
  // and it must not fire twice.
  Fired fireHold(const int key) {
    if (!valid(key)) return {};
    detectors_[key].reset();
    return resolve(key, button_gestures::Event::Hold);
  }

  // True while the router owes an answer on this key and the caller must keep its edges
  // from the rest of the firmware.
  bool busy(const int key) const { return valid(key) && detectors_[key].busy(); }

  // True when this key's edges must be routed through the detector at all. A key whose
  // single is still its own paging and which carries no double and no hold is left alone
  // entirely — that is what keeps page-turn latency and hold-to-repeat untouched for
  // everyone who never opened the Buttons screen.
  bool intercepts(const int key) const {
    if (!valid(key)) return false;
    const Binding& binding = bindings_[key];
    return binding.doubleClick != LP_MENU_DISABLED || binding.hold != LP_MENU_DISABLED ||
           !isNative(key, binding.single);
  }

  // True when the key's held state must be hidden as well as its edges. Only a bound hold
  // costs that, and it costs the key its hold-to-repeat: one key cannot both repeat while
  // held and fire a different action at half a second.
  bool suppressesHold(const int key) const { return valid(key) && bindings_[key].hold != LP_MENU_DISABLED; }

  void reset() {
    for (auto& detector : detectors_) detector.reset();
  }

 private:
  static bool valid(const int key) { return key >= 0 && key < KEY_COUNT; }

  // True when the binding is what this key already did. Kept apart from the rest because
  // it is answered by replaying the edge rather than by dispatching.
  bool isNative(const int key, const uint8_t function) const {
    if (function == LP_MENU_DISABLED) return false;
    return function == natives_[key].first || function == natives_[key].second;
  }

  Fired resolve(const int key, const button_gestures::Event event) const {
    uint8_t function = LP_MENU_DISABLED;
    switch (event) {
      case button_gestures::Event::Single:
        function = bindings_[key].single;
        break;
      case button_gestures::Event::Double:
        function = bindings_[key].doubleClick;
        break;
      case button_gestures::Event::Hold:
        function = bindings_[key].hold;
        break;
      case button_gestures::Event::None:
      default:
        return {};
    }
    if (function == LP_MENU_DISABLED) return {};
    Fired fired;
    fired.valid = true;
    fired.function = function;
    fired.replayRawEdge = isNative(key, function);
    return fired;
  }

  Binding bindings_[KEY_COUNT]{};
  Native natives_[KEY_COUNT]{NATIVE_SIDE_KEY, NATIVE_SIDE_KEY, NATIVE_HOME_KEY};
  button_gestures::Detector detectors_[KEY_COUNT]{};
};

}  // namespace button_router
