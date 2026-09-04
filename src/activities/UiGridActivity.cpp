#include "UiGridActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "ListSwipeGesture.h"
#include "MappedInputManager.h"
#include "components/PlainSliderBand.h"
#include "components/UIScale.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

UiGridActivity::UiGridActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity(name, renderer, mappedInput), UiAppHost(renderer) {}

void UiGridActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.on(ACTION_CELL, &UiGridActivity::cellTrampoline, this);
  app.on(ACTION_SLIDER, &UiGridActivity::sliderTrampoline, this);
  app.setScreen(&UiGridActivity::screenTrampoline, this);
  requestUpdate();
}

ListChrome UiGridActivity::chrome() const {
  ListChrome chrome;
  chrome.title = headerTitle();
  return chrome;
}

Rect UiGridActivity::gridPane() const {
  const list_chrome::Bands bands = listChromeBands(renderer, chrome());
  const int top = bands.contentTop + reservedHeight();
  const int height = std::max(0, bands.contentBottom - top);
  return Rect{0, top, renderer.getScreenWidth(), height};
}

// Two thumb-sized columns where a thumb exists, one column of full-width rows where
// only the four buttons do: the shape the settings screens had through lector-0.28.0.
settings_grid::Shape UiGridActivity::gridShape() const {
  if (mappedInput.hasTouch()) return settings_grid::Shape{};
  const auto& metrics = UITheme::getInstance().getMetrics();
  settings_grid::Shape shape;
  shape.columns = 1;
  shape.sidePad = 0;
  shape.gap = metrics.listRowGap;
  shape.minCellHeight = metrics.listRowHeight;
  shape.stretchToFill = false;
  return shape;
}

settings_grid::Layout UiGridActivity::gridLayout() const {
  const Rect pane = gridPane();
  return settings_grid::forPane(pane.width, pane.height, cellCount(), scrollRow_, gridShape());
}

void UiGridActivity::setSelected(const int index) {
  {
    // The render task reads the selection and the scroll row mid-build; a press
    // landing during a render would otherwise tear one against the other.
    RenderLock lock(*this);
    selected_ = index;
    scrollRow_ = settings_grid::scrollToShow(gridLayout(), selected_);
  }
  requestUpdate();
}

void UiGridActivity::clampSelection() {
  const int count = cellCount();
  selected_ = count > 0 ? std::clamp(selected_, 0, count - 1) : 0;
  scrollRow_ = count > 0 ? settings_grid::scrollToShow(gridLayout(), selected_) : 0;
}

void UiGridActivity::moveSelection(const int deltaRows, const int deltaCells) {
  const int count = cellCount();
  if (count == 0) return;
  setSelected(settings_grid::step(selected_, count, deltaRows, deltaCells, gridLayout().columns));
}

void UiGridActivity::armValueBand(const char* name, const int minValue, const int maxValue, const int smallStep,
                                  const int largeStep, const int current, std::function<void(int)> onChange,
                                  std::function<void()> onClose) {
  valueBand.show(name != nullptr ? name : "", minValue, maxValue, smallStep, largeStep, current, std::move(onChange),
                 std::move(onClose));
  requestUpdate();
}

void UiGridActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<UiGridActivity*>(user)->buildScreen(screen);
}

void UiGridActivity::cellTrampoline(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<UiGridActivity*>(user);
  if (event.value < 0 || event.value >= self->cellCount()) return;
  self->selected_ = event.value;
  self->activateCell(event.value);
}

void UiGridActivity::sliderTrampoline(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<UiGridActivity*>(user);
  if (!self->valueBand.isActive()) return;
  const auto update = [self] { self->requestUpdate(); };
  if (event.dragPermille >= 0) {
    self->valueBand.setFromPermille(event.dragPermille, update);
    return;
  }
  self->valueBand.adjustBy(event.value, update);
}

void UiGridActivity::buildScreen(UiScreen& screen) {
  const list_chrome::Bands bands = listChromeBands(renderer, chrome());
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(bands.contentTop), 0,
                                      static_cast<int16_t>(renderer.getScreenHeight() - bands.contentBottom), 0});

  if (valueBand.isActive()) buildValueBand(screen);

  const Rect pane = gridPane();
  const settings_grid::Layout layout = gridLayout();
  const int count = cellCount();
  for (int i = 0; i < count; ++i) {
    const settings_grid::Rect rect = settings_grid::cellAt(layout, pane.y, i);
    if (rect.width == 0) continue;  // scrolled out
    buildCell(screen, i, rect);
  }
}

// One cell: a box that answers to a tap, its name in the small face over its
// value in the body face, both centred and both cut to the cell rather than run
// out of it. The two grids used to carry a copy of this each, and only one of
// them truncated.
void UiGridActivity::buildCell(UiScreen& screen, const int index, const settings_grid::Rect& rect) {
  const auto& theme = screen.theme();
  auto& target = screen.frame().target();
  const bool selected = index == selected_;
  const fui::Rect box{static_cast<int16_t>(rect.x), static_cast<int16_t>(rect.y), static_cast<int16_t>(rect.width),
                      static_cast<int16_t>(rect.height)};

  if (!mappedInput.hasTouch()) {
    buildRow(screen, index, box);
    return;
  }

  fui::ButtonProps props;
  props.action = ACTION_CELL;
  props.value = static_cast<int16_t>(index);
  props.state = selected ? fui::StateChecked : fui::StateNormal;
  props.styles = theme.button;
  if (!mappedInput.hasTouch()) {
    // The keys-only grid always outlined its cells; only the touch grid reads
    // them as buttons.
    props.styles = fui::defaultButtonStyles();
    props.styles.normal.border = fui::Paint::solid(fui::Color::Black);
    props.styles.normal.borderWidth = 1;
  }
  props.radius = static_cast<uint8_t>(theme.controlRadius);
  props.minTouchSize = screen.frame().device().minTouchSize;
  screen.button(props, box);

  fui::TextStyle name = theme.smallText;
  name.align = fui::TextAlign::Center;
  name.inverted = selected;
  name.maxLines = 2;
  fui::TextStyle value = theme.bodyText;
  value.align = fui::TextAlign::Center;
  value.inverted = selected;

  const int16_t nameHeight = target.lineHeight(name.font);
  const int16_t valueHeight = target.lineHeight(value.font);
  const int16_t top = static_cast<int16_t>(box.y + (box.height - nameHeight - valueHeight) / 2);
  const int16_t inset = theme.spaceSm;
  const fui::Rect line{static_cast<int16_t>(box.x + inset), top, static_cast<int16_t>(box.width - inset * 2),
                       nameHeight};
  if (cellName(index) != nullptr) target.text(line, cellName(index), name);
  if (cellValue(index) != nullptr) {
    target.text(fui::Rect{line.x, static_cast<int16_t>(top + nameHeight), line.width, valueHeight}, cellValue(index),
                value);
  }
}

// One row of the keys-only list: the name on the left, its value against the right
// edge, the selected one reversed. The same two pieces of text the cell stacks, laid
// out the way GUI.drawList laid them out before the grid.
void UiGridActivity::buildRow(UiScreen& screen, const int index, const fui::Rect& box) {
  const auto& theme = screen.theme();
  auto& target = screen.frame().target();
  const bool selected = index == selected_;

  fui::ButtonProps props;
  props.action = ACTION_CELL;
  props.value = static_cast<int16_t>(index);
  props.state = selected ? fui::StateChecked : fui::StateNormal;
  props.styles = fui::plainStyles();
  props.styles.selected.background = fui::Paint::solid(fui::Color::Black);
  props.styles.selected.foreground = fui::Paint::solid(fui::Color::White);
  props.minTouchSize = 0;
  screen.button(props, box);

  const int16_t inset = theme.spaceSm;
  const int16_t lineHeight = target.lineHeight(theme.bodyText.font);

  fui::TextStyle name = theme.bodyText;
  name.align = fui::TextAlign::Left;
  name.inverted = selected;
  name.maxLines = 2;

  fui::TextStyle value = theme.smallText;
  value.align = fui::TextAlign::Right;
  value.inverted = selected;

  const char* valStr = cellValue(index);
  int16_t valWidth = 0;
  if (valStr != nullptr && valStr[0] != '\0') {
    valWidth = static_cast<int16_t>(target.measureText(value.font, valStr, value).width);
  }
  const int16_t valGap = valWidth > 0 ? static_cast<int16_t>(theme.spaceSm) : 0;
  const int16_t nameWidth = static_cast<int16_t>(box.width - inset * 2 - valWidth - valGap);

  const fui::Rect nameRect{static_cast<int16_t>(box.x + inset), box.y, nameWidth, box.height};
  const fui::Rect valueRect{static_cast<int16_t>(box.x + inset),
                            static_cast<int16_t>(box.y + (box.height - lineHeight) / 2),
                            static_cast<int16_t>(box.width - inset * 2), lineHeight};

  if (cellName(index) != nullptr) target.text(nameRect, cellName(index), name);
  if (valStr != nullptr) target.text(valueRect, valStr, value);
}

// The armed number, in the header's place. On the touch board that is a slider
// row, so the capsule and the two step buttons come from the theme and a drag
// arrives through the same interaction table as every tap; on the keys-only
// boards it is the filled band with a plain bar that it has always been.
void UiGridActivity::buildValueBand(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& theme = screen.theme();
  char valueText[16];
  snprintf(valueText, sizeof(valueText), "%d", valueBand.value());

  // The band spans the header's own rect, so nothing below it moves when a value
  // is armed.
  const int16_t height = static_cast<int16_t>(metrics.headerHeight + metrics.verticalSpacing + theme.rowHeight);
  const fui::Rect rect{0, static_cast<int16_t>(metrics.topPadding), static_cast<int16_t>(renderer.getScreenWidth()),
                       height};
  const int span = valueBand.maxValue() > valueBand.minValue() ? valueBand.maxValue() - valueBand.minValue() : 1;

  if (!mappedInput.hasTouch()) {
    // Keys-only boards get the filled band they always had: the name and its
    // readout in reverse over a plain bar, no capsule and no step buttons.
    plain_slider_band::draw(screen, rect, valueBand.name().c_str(), valueText, valueBand.value() - valueBand.minValue(),
                            span, /*inverted=*/true);
    return;
  }

  fui::SliderRowProps props;
  props.label = valueBand.name().c_str();
  props.value = valueText;
  props.sliderValue = valueBand.value() - valueBand.minValue();
  props.max = span;
  props.sliderAction = ACTION_SLIDER;
  props.decrement = ACTION_SLIDER;
  props.increment = ACTION_SLIDER;
  props.decrementValue = static_cast<int16_t>(-valueBand.smallStep());
  props.incrementValue = static_cast<int16_t>(valueBand.smallStep());
  props.buttonRadius = static_cast<uint8_t>(theme.controlRadius);
  props.labelText = theme.smallText;
  props.valueText = theme.smallText;

  fui::sliderRow(screen.frame(), rect, props);
}

void UiGridActivity::loop() {
  if (handleCustomInput()) return;
  if (valueBand.handleInput(mappedInput, [this] { requestUpdate(); })) {
    // The band owns the keys while it is up. A touch still routes below, so the
    // capsule and its two buttons answer; anything else puts the band away.
    const auto route = UiAppHost::routeTouch(mappedInput, /*withLongPress=*/false, /*routeHeld=*/true);
    if (route.routed && app.invalidated()) requestUpdate();
    int tx = 0;
    int ty = 0;
    if (!route && mappedInput.wasScreenTapped(tx, ty)) {
      // A touch anywhere else puts the band away, the same bargain a pop-up makes.
      valueBand.close([this] { requestUpdate(); });
    }
    return;
  }

  const auto route = UiAppHost::routeTouch(mappedInput);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) return;

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selected_ >= 0 && selected_ < cellCount()) activateCell(selected_);
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBackButton();
    return;
  }

  // Rows on the side pair, cells on the front pair, and each press counted once.
  // ScreenUp/ScreenDown/ScreenLeft/ScreenRight rather than the raw buttons, so a
  // rotated screen keeps moving the way the hints under it say it does.
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::ScreenDown}, [this] { moveSelection(1, 0); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::ScreenUp}, [this] { moveSelection(-1, 0); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::ScreenLeft}, [this] { moveSelection(0, -1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::ScreenRight}, [this] { moveSelection(0, 1); });

  // A swipe scrolls the grid by a row.
  switch (mappedInput.wasListScrollSwipe()) {
    case list_swipe::Scroll::PageDown:
      moveSelection(1, 0);
      break;
    case list_swipe::Scroll::PageUp:
      moveSelection(-1, 0);
      break;
    default:
      break;
  }
}

void UiGridActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const ListChrome bands = chrome();
  // An armed number takes the header's place, so the title is not drawn under it.
  if (!valueBand.isActive()) drawListChromeTop(renderer, bands);
  renderUi();
  if (reservedHeight() > 0) {
    const list_chrome::Bands measured = listChromeBands(renderer, bands);
    drawReserved(Rect{0, measured.contentTop, renderer.getScreenWidth(), reservedHeight()});
  }
  drawListChromeBottom(renderer, mappedInput, bands);
  if (drawOverlay()) return;
  renderer.displayBuffer();
}
