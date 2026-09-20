#include "UiAppHost.h"

#include "UiAppHelpers.h"

namespace fui = freeink::ui;

void GatedApp::on(const fui::ActionId action, const ActionHandler handler, void* const user) {
  for (uint8_t i = 0; i < boundCount_; ++i) {
    if (bound_[i].action != action) continue;
    bound_[i] = Bound{action, handler, user};
    Base::on(action, &GatedApp::gateTrampoline, this);
    return;
  }
  if (boundCount_ >= kMaxBound) return;
  bound_[boundCount_++] = Bound{action, handler, user};
  Base::on(action, &GatedApp::gateTrampoline, this);
}

void GatedApp::gateTrampoline(const fui::ActionEvent& event, void* const user) {
  static_cast<GatedApp*>(user)->handle(event);
}

void GatedApp::handle(const fui::ActionEvent& event) {
  if (host_ == nullptr) {
    forward(event);
    return;
  }
  // A drag carries a position rather than a discrete press (dragPermille >= 0),
  // and a long press is already a deliberate held gesture that confirms itself.
  // Neither is a control a first tap can arm.
  const bool gated = event.dragPermille < 0 && !event.longPress && !host_->twoTapExempt(event.action);
  if (gated && host_->twoTap().decide(event.action, event.value) == two_tap::Decision::Arm) {
    host_->noteArmed(event);
    return;
  }
  forward(event);
}

void GatedApp::forward(const fui::ActionEvent& event) const {
  for (uint8_t i = 0; i < boundCount_; ++i) {
    if (bound_[i].action == event.action && bound_[i].handler != nullptr) {
      bound_[i].handler(event, bound_[i].user);
      return;
    }
  }
}

UiAppHost::UiAppHost(const GfxRenderer& renderer)
    : uiTarget(makeUiTarget(renderer)), app(uiTarget, uiTarget.deviceContext()) {
  app.bindHost(this);
}

void UiAppHost::resetUi() {
  uiReady = false;
  app.clearBoundHandlers();
  twoTapGate.clear();
  exemptCount = 0;
  armHook = nullptr;
  armHookUser = nullptr;
  applySharedUiTheme(app, uiTarget);
}

void UiAppHost::renderUi() {
  app.setDevice(uiTarget.deviceContext());
  app.render();
  uiReady = true;
}

void UiAppHost::exemptFromTwoTap(const fui::ActionId action) {
  if (exemptCount >= kMaxExempt) return;
  exempt[exemptCount++] = action;
}

bool UiAppHost::twoTapExempt(const fui::ActionId action) const {
  for (uint8_t i = 0; i < exemptCount; ++i) {
    if (exempt[i] == action) return true;
  }
  return false;
}

void UiAppHost::noteArmed(const fui::ActionEvent& event) {
  armedThisPass = true;
  if (armHook != nullptr) armHook(event, armHookUser);
}

void UiAppHost::clearTwoTap() {
  if (!twoTapGate.armed()) return;
  twoTapGate.clear();
  // The highlight IS the SDK's active element, so dropping the arm has to drop
  // that too. An off-target release clears it and dispatches nothing.
  fui::InputSnapshot off{};
  off.touchReleased = true;
  off.touchX = -1;
  off.touchY = -1;
  app.route(off);
  app.invalidate(fui::RefreshHint::Fast);
}

UiAppHost::TouchRoute UiAppHost::routeTouch(const MappedInputManager& input, const bool withLongPress,
                                            const bool routeHeld) {
  TouchRoute result;  // named apart from route() — cppcheck flags the shadow
  if (!uiReady) return result;
  // Runtime, not compile time: the `default` environment builds one binary for
  // the keys-only boards and the X4 Pro (lib/hal/DeviceProfile.h).
  twoTapGate.setEnabled(input.hasTouch());
  // A physical key — or the on-screen hint band, which stands in for the front
  // buttons the X4 Pro does not have — acts on the screen's own selection. An
  // arm left behind would be a second, stale highlight on another row.
  if (input.wasAnyPressed() || input.tappedHintHardware() >= 0) clearTwoTap();
  result.snap = touchSnapshotFrom(input, withLongPress);
  if (!result.snap.touchPressed && !result.snap.touchReleased && !(routeHeld && result.snap.touchHeld)) {
    return result;
  }
  result.routed = true;
  armedThisPass = false;
  result.event = app.route(result.snap);
  if (!result.snap.touchReleased) return result;
  if (armedThisPass) {
    // Routing a release clears the SDK's active element, which is what paints
    // the armed control outlined, so re-take it at the point the tap landed. A
    // press-only snapshot matches no release and therefore dispatches nothing.
    fui::InputSnapshot press{};
    press.touchPressed = true;
    press.touchX = result.snap.touchX;
    press.touchY = result.snap.touchY;
    app.route(press);
    app.invalidate(fui::RefreshHint::Fast);
  } else if (!result.event) {
    // A tap that landed on nothing, or the release that ends a scroll swipe.
    clearTwoTap();
  }
  return result;
}

fui::ActionEvent UiAppHost::route(const fui::InputSnapshot& snap) {
  if (!uiReady) return {};
  return app.route(snap);
}
