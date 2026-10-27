#include "UiStatusActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/ComparisonLayout.h"
#include "components/LightPanelGeometry.h"
#include "components/SignalMeter.h"
#include "components/SliderField.h"
#include "components/SliderLook.h"
#include "components/StatusStack.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "util/QrUtils.h"

namespace fui = freeink::ui;

void UiStatusActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.on(ACTION_ACCEPT, &UiStatusActivity::acceptTrampoline, this);
  app.on(ACTION_CANCEL, &UiStatusActivity::cancelTrampoline, this);
  app.on(ACTION_CHOICE, &UiStatusActivity::choiceTrampoline, this);
  app.on(ACTION_LIST, &UiStatusActivity::listTrampoline, this);
  app.on(ACTION_SLIDER, &UiStatusActivity::sliderTrampoline, this);
  // EXEMPT from two-tap confirmation: a slider is a continuous value, not a
  // discrete choice. Its capsule is dragged and its two step buttons are
  // pressed repeatedly, so a confirming tap per step would make brightness and
  // font size unusable, and a drag has no discrete control to arm at all.
  exemptFromTwoTap(ACTION_SLIDER);
  // Same for the number pad: arming each digit before it typed would make every number
  // take two taps per figure.
  app.on(ACTION_KEYPAD, &UiStatusActivity::keypadTrampoline, this);
  exemptFromTwoTap(ACTION_KEYPAD);
  keypadValue_ = -1;
  setArmHook(&UiStatusActivity::armTrampoline, this);
  app.setScreen(&UiStatusActivity::screenTrampoline, this);
  requestUpdate();
}

void UiStatusActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<UiStatusActivity*>(user)->buildScreen(screen);
}

void UiStatusActivity::acceptTrampoline(const fui::ActionEvent&, void* user) {
  static_cast<UiStatusActivity*>(user)->onConfirmButton();
}

void UiStatusActivity::cancelTrampoline(const fui::ActionEvent&, void* user) {
  static_cast<UiStatusActivity*>(user)->onBackButton();
}

void UiStatusActivity::choiceTrampoline(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<UiStatusActivity*>(user);
  if (event.value < 0 || event.value >= self->choiceCount_) return;
  self->choiceIndex_ = event.value;
  self->onChoiceActivated(event.value);
}

void UiStatusActivity::armTrampoline(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<UiStatusActivity*>(user);
  if (event.action == ACTION_LIST) {
    if (event.value >= 0 && event.value < self->listCount_) self->listNav_.selected = event.value;
    return;
  }
  if (event.action == ACTION_CHOICE) {
    if (event.value >= 0 && event.value < self->choiceCount_) self->choiceIndex_ = event.value;
  }
}

void UiStatusActivity::listTrampoline(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<UiStatusActivity*>(user);
  if (event.value < 0 || event.value >= self->listCount_) return;
  self->listNav_.selected = event.value;
  self->onListActivated(event.value);
}

// A drag reports where along the track the finger is; a step button reports how
// far to move. Both land on the same handler, clamped by the view's own range.
void UiStatusActivity::sliderTrampoline(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<UiStatusActivity*>(user);
  const StatusView view = self->statusView();
  if (!view.showSlider) return;
  const slider_field::Range range{view.sliderMin, view.sliderMax};
  const int next = event.dragPermille >= 0 ? slider_field::valueForPermille(event.dragPermille, range)
                                           : slider_field::clamp(view.sliderValue + event.value, range);
  if (next == view.sliderValue) return;
  self->onSliderChanged(next);
}

void UiStatusActivity::keypadTrampoline(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<UiStatusActivity*>(user);
  const StatusView view = self->statusView();
  if (!view.showSlider) return;
  const int current = view.sliderValue;
  const bool typing = current == self->keypadValue_;
  if (event.value == fui::QWERTY_KEY_ENTER) {
    self->onConfirmButton();
    return;
  }
  int next;
  if (event.value == fui::QWERTY_KEY_BACKSPACE) {
    next = current / 10;
  } else {
    const int digit = event.value - '0';
    if (digit < 0 || digit > 9) return;
    next = typing ? current * 10 + digit : digit;
    // A digit that would run past the end is dropped, so a slip on 4 7 9 keeps 47.
    if (next > view.sliderMax) return;
  }
  // Typing can pass through a value under the minimum ("1" on the way to "12" of a
  // range that starts at 5); the subclass holds it as typed, OK clamps it.
  self->keypadValue_ = next;
  if (next != current) self->onSliderChanged(next);
}

void UiStatusActivity::setListSelectionLocked(const int index) {
  listNav_.selected = index;
  listNav_.follow(listCount_);
}

void UiStatusActivity::setListSelection(const int index) {
  // No render lock, same as UiListActivity::moveSelectionTo: blocking here parked
  // the main loop for a whole e-ink refresh and dropped presses inside it. The
  // viewport pull is deferred to the next build (syncToProps).
  listNav_.selected = index;
  listNav_.followOnBuild = true;
  requestUpdate();
}

void UiStatusActivity::buildScreen(UiScreen& screen) {
  const StatusView view = statusView();
  qrPlacements_ = {};
  hasSlider_ = view.showSlider;

  // The header (and its sub-header) are painted outside the app, same as every
  // list screen, so the body starts under whichever of them was drawn.
  const auto& metrics = UITheme::getInstance().getMetrics();
  int16_t bodyTop = static_cast<int16_t>(
      metrics.topPadding +
      (view.title ? BaseTheme::headerHeightFor(renderer, renderer.getScreenWidth(), view.title, view.headerRight)
                  : metrics.headerHeight) +
      metrics.verticalSpacing);
  if (view.subtitleLeft) bodyTop = static_cast<int16_t>(bodyTop + metrics.tabBarHeight);
  screen.setContentMargin(fui::Insets{bodyTop, static_cast<int16_t>(metrics.contentSidePadding),
                                      static_cast<int16_t>(metrics.buttonHintsHeight),
                                      static_cast<int16_t>(metrics.contentSidePadding)});

  // Taken from the bottom first, so neither shape lays text into the band the
  // buttons stand in.
  buildActions(screen, view);

  buildChoiceBand(screen, view);

  if (view.listItems != nullptr && view.listCount > 0) {
    buildList(screen, view);
    return;
  }
  listCount_ = 0;

  if (view.comparison[0].label != nullptr) {
    buildComparison(screen, view);
    return;
  }
  if (view.sections[0].heading != nullptr || view.sections[0].paragraph != nullptr) {
    buildSections(screen, view);
    return;
  }
  buildCentredLines(screen, view);
}

void UiStatusActivity::buildActions(UiScreen& screen, const StatusView& view) {
  const bool hasCancel = view.cancelLabel != nullptr && view.cancelLabel[0] != '\0';
  const bool hasAccept = view.acceptLabel != nullptr && view.acceptLabel[0] != '\0';
  if (!hasCancel && !hasAccept) return;

  const auto& theme = screen.theme();
  const int16_t height = static_cast<int16_t>(theme.rowHeight + theme.spaceSm * 2);
  const fui::Rect band = screen.takeBottom(height, theme.spaceSm);
  const int16_t gap = theme.spaceSm;
  const int16_t width = hasCancel && hasAccept ? static_cast<int16_t>((band.width - gap) / 2) : band.width;

  fui::ButtonProps props;
  props.text = theme.bodyText;
  props.styles = theme.button;
  props.minTouchSize = screen.frame().device().minTouchSize;
  props.radius = static_cast<uint8_t>(theme.controlRadius);
  if (hasCancel) {
    props.label = view.cancelLabel;
    props.action = ACTION_CANCEL;
    screen.button(props, fui::Rect{band.x, band.y, width, band.height});
  }
  if (hasAccept) {
    props.label = view.acceptLabel;
    props.action = ACTION_ACCEPT;
    const int16_t x = hasCancel ? static_cast<int16_t>(band.x + band.width - width) : band.x;
    screen.button(props, fui::Rect{x, band.y, width, band.height});
  }
}

bool UiStatusActivity::moveChoice(const int delta) {
  if (choiceCount_ < 2) return false;
  int next = choiceIndex_ + delta;
  while (next < 0) next += choiceCount_;
  choiceIndex_ = next % choiceCount_;
  requestUpdate();
  return true;
}

bool UiStatusActivity::navigateList() {
  if (listCount_ <= 0) return false;
  bool moved = false;
  listButtons_.onListNav(listNav_.selected, listCount_, listNav_.pageRows(), [&](const int index) {
    setListSelection(index);
    moved = true;
  });
  return moved;
}

void UiStatusActivity::loop() {
  if (handleCustomInput()) return;

  // The screen's own buttons, when it drew any: the interaction table the last
  // render published is what a tap is measured against.
  const auto route = UiAppHost::routeTouch(mappedInput, /*withLongPress=*/false, /*routeHeld=*/hasSlider_);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) return;

  // Any direction steps between the answers: with two of them there is no
  // meaningful difference between next and previous, and both readers reach for
  // whichever key is under the thumb.
  if (choiceCount_ > 1) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (moveChoice(-1)) return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
        mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (moveChoice(1)) return;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBackButton();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (listCount_ > 0) {
      onListActivated(listNav_.selected);
      return;
    }
    if (choiceCount_ > 0) {
      onChoiceActivated(choiceIndex_);
      return;
    }
    onConfirmButton();
    return;
  }

  navigateList();
}

static int metricsSpacing() { return UITheme::getInstance().getMetrics().verticalSpacing; }

void UiStatusActivity::render(RenderLock&&) {
  const StatusView view = statusView();
  if (view.hidden) return;

  renderer.clearScreen();
  if (view.title) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int pageWidth = renderer.getScreenWidth();
    const Rect headerRect{0, metrics.topPadding, pageWidth,
                          BaseTheme::headerHeightFor(renderer, pageWidth, view.title, view.headerRight)};
    GUI.drawHeader(renderer, headerRect, view.title, nullptr, view.headerRight);
    drawHeaderExtras(headerRect);
    if (view.subtitleLeft) {
      const int bandTop = headerRect.y + headerRect.height;
      GUI.drawSubHeader(renderer, Rect{0, bandTop, pageWidth, metrics.tabBarHeight}, view.subtitleLeft,
                        view.subtitleRight);
      if (view.showSignal) {
        drawSignal(view, pageWidth - metrics.contentSidePadding,
                   bandTop + metrics.tabBarHeight - metrics.verticalSpacing);
      }
    }
  }
  renderUi();
  // The codes are bitmaps, not FreeInkUI elements: the body layout said where
  // they go, and they land in the same buffer the app just drew into.
  drawQrCodes();
  if (listCount_ > 0) {
    // Same chevrons as every UiListActivity, in the spacing outside the rows.
    const list_scrollbar::Arrows arrows = list_scrollbar::forWindow(listCount_, listNav_.top, listNav_.pageRows());
    GUI.drawScrollArrows(renderer, list_scrollbar::outsideBand(listBand_, metricsSpacing()), arrows);
  }

  const auto labels =
      mappedInput.mapLabels(view.backHint ? view.backHint : tr(STR_BACK), view.confirmHint ? view.confirmHint : "",
                            view.thirdHint ? view.thirdHint : "", view.fourthHint ? view.fourthHint : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (drawOverlay()) return;
  // Same contract as UiListActivity: a popup painted over this screen (a failure
  // message, an option list) leaves its band behind, and the next paint cleans it.
  const bool overlayDirty = UITheme::consumeOverlayPainted();
  const bool clean = cleanPaint_ || view.title != lastTitle_ || overlayDirty;
  cleanPaint_ = false;
  lastTitle_ = view.title;
  renderer.displayBuffer(clean && view.refresh == HalDisplay::FAST_REFRESH ? HalDisplay::HALF_REFRESH : view.refresh);
  afterRender();
}
