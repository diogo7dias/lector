#pragma once
#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>

#include <atomic>

#include "TwoTapGate.h"

class GfxRenderer;
class MappedInputManager;
class UiAppHost;

// FreeInkApp dispatches an action from inside route()/render(), so a two-tap
// gate placed after either call has already let the action run. Every handler a
// screen registers is therefore registered as one trampoline of ours, with the
// real handler kept here: a single interception point for every FreeInkUI
// screen in the firmware. No screen opts in — it calls on() exactly as before —
// so no screen can forget to.
class GatedApp : public freeink::ui::FreeInkApp<24, 6> {
 public:
  using Base = freeink::ui::FreeInkApp<24, 6>;
  using Base::Base;

  void bindHost(UiAppHost* host) { host_ = host; }

  // Hides Base::on. Every `app.on(...)` call site names a GatedApp, so lookup
  // finds this one without any of them changing.
  void on(freeink::ui::ActionId action, ActionHandler handler, void* user = nullptr);

  // Drops the remembered handlers on screen entry, alongside the base's own
  // reset (UiAppHost::resetUi).
  void clearBoundHandlers() { boundCount_ = 0; }

 private:
  static void gateTrampoline(const freeink::ui::ActionEvent& event, void* user);
  void handle(const freeink::ui::ActionEvent& event);
  void forward(const freeink::ui::ActionEvent& event) const;

  struct Bound {
    freeink::ui::ActionId action = freeink::ui::NO_ACTION;
    ActionHandler handler = nullptr;
    void* user = nullptr;
  };
  // One slot per action, matching the base's handler capacity.
  static constexpr uint8_t kMaxBound = 6;
  Bound bound_[kMaxBound]{};
  uint8_t boundCount_ = 0;
  UiAppHost* host_ = nullptr;
};

// Owns the FreeInkApp hosting protocol every FUI screen shares, list-shaped or
// not: the font-bound render target, the app, and the uiReady handshake that
// lets the loop task route touch snapshots against the interaction table the
// render task rebuilds. FreeInkApp keeps the last complete interaction table
// published while it builds the next one, so the gate closes only when the
// old table is semantically invalid (screen reset/explicit close), not around
// an ordinary repaint. The handshake lives here exactly once so no screen
// re-implements (and mis-orders) it.
//
// UiListActivity layers list navigation on top of this; screens that are not a
// single list (sliders, prompts, state machines) inherit or hold this directly
// and keep their own input/loop logic.
class UiAppHost {
 public:
  // One shared instantiation for every FUI screen (the largest of the sizes
  // the screens used to pick individually): FreeInkApp, Screen and Frame are
  // capacity-templated, so per-screen capacities each minted a fresh copy of
  // that code in flash. The wider interaction buffer costs ~300 bytes of RAM
  // per live host, bounded by the activity stack depth.
  using UiApp = GatedApp;
  using UiScreen = UiApp::ScreenType;

  explicit UiAppHost(const GfxRenderer& renderer);

  // Screen-entry reset: close the routing gate and rebind the shared theme
  // tokens (refreshed for the active UITheme + this target's fonts). Call from
  // onEnter/loadOnce before registering actions and the screen fn.
  void resetUi();

  // Re-derives the device context, renders into FreeInkApp's non-published
  // interaction-table generation, then opens uiReady after the first publish.
  // Once open, routing stays available during later renders through the last
  // complete published generation. Call from the render task wherever the app
  // should paint; chrome before, hints after.
  void renderUi();

  // What loop-task routing saw this pass. `routed` is true when the gate was
  // open and the snapshot carried relevant touch input — the invalidated()
  // repaint check belongs behind it, so a pending render requested elsewhere
  // is not re-requested on every idle pass.
  struct TouchRoute {
    freeink::ui::ActionEvent event{};
    freeink::ui::InputSnapshot snap{};
    bool routed = false;
    explicit operator bool() const { return static_cast<bool>(event); }
  };

  // Gated snapshot-build + route: the common loop head. withLongPress forwards
  // the SDK long-press (rows must carry InputLongPress); routeHeld forwards
  // held frames for InputDrag elements (sliders, drag-select fields).
  TouchRoute routeTouch(const MappedInputManager& input, bool withLongPress = false, bool routeHeld = false);

  // Gated route of a caller-built snapshot, for flows that need the snapshot
  // before dispatch (e.g. a handler that reads "was this a release" state).
  freeink::ui::ActionEvent route(const freeink::ui::InputSnapshot& snap);

  // Close the routing gate outside a render, e.g. when the data the
  // interaction table indexes is released mid-state. Reopens on renderUi().
  void closeRouting() { uiReady = false; }
  // Whether routing is live, for a screen that must distinguish "the gate was
  // shut" from "the gate was open and nothing was under the finger" — a slider
  // swallowing the tail of its own drag needs that difference.
  bool routingReady() const { return uiReady.load(std::memory_order_acquire); }

  // --- two-tap touch confirmation (see components/TwoTapGate.h) -------------

  // Actions a first tap must NOT arm. Stated at the registration site with the
  // reason it is exempt; nothing is inferred here.
  void exemptFromTwoTap(freeink::ui::ActionId action);
  bool twoTapExempt(freeink::ui::ActionId action) const;
  // Drops the arm and the highlight it paints. Every lifetime rule routes here:
  // leaving the screen, a scroll, a physical key, a tap on empty space.
  void clearTwoTap();
  two_tap::Gate& twoTap() { return twoTapGate; }
  // Run when a first tap arms a control rather than running it, so a screen can
  // move its own selection onto the armed control. Without it the side keys and
  // the on-screen Confirm would act on a different row than the highlight.
  using ArmHook = void (*)(const freeink::ui::ActionEvent&, void*);
  void setArmHook(ArmHook hook, void* user) {
    armHook = hook;
    armHookUser = user;
  }
  // Called by GatedApp when the gate arms instead of forwarding.
  void noteArmed(const freeink::ui::ActionEvent& event);

  freeink::ui::GfxRendererTarget uiTarget;  // must precede `app`: the app holds a reference to it
  UiApp app;

 private:
  // Opened by the render task after publication and closed on lifecycle/state
  // resets; read by the loop task (route*).
  std::atomic<bool> uiReady{false};

  two_tap::Gate twoTapGate;
  // Four is every exemption the firmware has (see the call sites); an extra one
  // is dropped rather than silently gating something it should not.
  static constexpr uint8_t kMaxExempt = 4;
  freeink::ui::ActionId exempt[kMaxExempt]{};
  uint8_t exemptCount = 0;
  ArmHook armHook = nullptr;
  void* armHookUser = nullptr;
  // Set by noteArmed() for the pass routeTouch() is in, so it knows to repaint
  // the highlight rather than treat the tap as an action.
  bool armedThisPass = false;
};
