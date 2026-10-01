#include "UiGridActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <algorithm>

#include "ListSwipeGesture.h"
#include "MappedInputManager.h"
#include "components/ContentsLook.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "util/HoldRepeat.h"

namespace fui = freeink::ui;

UiGridActivity::UiGridActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity(name, renderer, mappedInput), UiAppHost(renderer) {}

void UiGridActivity::onEnter() {
  Activity::onEnter();
  contents_look::bindFonts(uiTarget);
  resetUi();
  app.on(ACTION_CELL, &UiGridActivity::cellTrampoline, this);
  app.setScreen(&UiGridActivity::screenTrampoline, this);
  requestUpdate();
}

ListChrome UiGridActivity::shownChrome() const {
  ListChrome shown = chrome();
  toContentsLook(shown, mappedInput.hasTouch());
  return shown;
}

ListChrome UiGridActivity::chrome() const {
  ListChrome chrome;
  chrome.title = headerTitle();
  return chrome;
}

Rect UiGridActivity::gridPane() const {
  const list_chrome::Bands bands = listChromeBands(renderer, shownChrome());
  const int top = bands.contentTop + reservedHeight();
  const int height = std::max(0, bands.contentBottom - top);
  return Rect{0, top, renderer.getScreenWidth(), height};
}

// A row, and the heading over it when it opens a group: the window then keeps a group's
// heading on screen with its first row.
int UiGridActivity::rowHeightFor(const int index) const {
  return fui::contents::ROW_H + (cellHeading(index) != nullptr ? fui::contents::HEAD_H : 0);
}

wrapped_list::Window UiGridActivity::keysOnlyWindow() const {
  return wrapped_list::window(cellCount(), selected_, scrollRow_, gridPane().height, 0,
                              [this](const int index) { return rowHeightFor(index); });
}

void UiGridActivity::setSelected(const int index) {
  {
    // The render task reads the selection and the scroll row mid-build; a press
    // landing during a render would otherwise tear one against the other.
    RenderLock lock(*this);
    selected_ = index;
    scrollRow_ = keysOnlyWindow().first;
  }
  requestUpdate();
}

void UiGridActivity::clampSelection() {
  const int count = cellCount();
  selected_ = count > 0 ? std::clamp(selected_, 0, count - 1) : 0;
  if (count <= 0) {
    scrollRow_ = 0;
    return;
  }
  scrollRow_ = keysOnlyWindow().first;
}

void UiGridActivity::moveSelection(const int deltaRows, const int deltaCells) {
  const int count = cellCount();
  if (count == 0) return;
  setSelected(settings_grid::step(selected_, count, deltaRows, deltaCells, /*columns=*/1));
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

void UiGridActivity::buildScreen(UiScreen& screen) {
  const list_chrome::Bands bands = listChromeBands(renderer, shownChrome());
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(bands.contentTop), 0,
                                      static_cast<int16_t>(renderer.getScreenHeight() - bands.contentBottom), 0});

  // One column, each row as tall as its heading makes it. The window keeps the selection
  // on screen and hands back where it starts, so a later press scrolls from there.
  const wrapped_list::Window win = keysOnlyWindow();
  scrollRow_ = win.first;
  buildContents(screen, gridPane(), win);
}

void UiGridActivity::buildContents(UiScreen& screen, const Rect& pane, const wrapped_list::Window& win) {
  // Every cell with its heading, so the headings above the window still count toward
  // the numerals; the list starts drawing at the window's first row. Three strings a
  // cell (heading, name, value), sized once so the items can point into them: the
  // subclass hands its name and value out of shared scratch.
  const int count = cellCount();
  contentsText_.assign(static_cast<size_t>(count) * 3, std::string());
  contentsItems_.clear();
  contentsItems_.reserve(static_cast<size_t>(count) * 2);
  int topItem = 0;
  int selectedItem = -1;
  for (int i = 0; i < count; ++i) {
    std::string* text = &contentsText_[static_cast<size_t>(i) * 3];
    if (i == win.first) topItem = static_cast<int>(contentsItems_.size());
    if (const char* heading = cellHeading(i)) {
      text[0] = heading;
      fui::ListItem item{};
      item.isHeader = true;
      item.label = text[0].c_str();
      contentsItems_.push_back(item);
    }
    if (const char* name = cellName(i)) text[1] = name;
    if (const char* value = cellValue(i)) text[2] = value;
    fui::ListItem item{};
    item.label = text[1].c_str();
    item.value = text[2].empty() ? nullptr : text[2].c_str();
    item.actionValue = static_cast<int16_t>(i);
    if (i == selected_) selectedItem = static_cast<int>(contentsItems_.size());
    contentsItems_.push_back(item);
  }

  fui::ListProps props{};
  props.items = contentsItems_.data();
  props.count = static_cast<uint16_t>(contentsItems_.size());
  props.topIndex = static_cast<uint16_t>(topItem);
  props.selectedIndex = static_cast<int16_t>(selectedItem);
  props.action = ACTION_CELL;
  contents_look::applyListProps(props);
  // The body starts at the chrome; the pane starts under the reserved band (the preview).
  if (pane.y > screen.body().y) screen.spacer(static_cast<int16_t>(pane.y - screen.body().y));
  screen.list(props, static_cast<int16_t>(pane.height));

  scrollArrowBand_ = list_scrollbar::outsideBand(pane, UITheme::getInstance().getMetrics().verticalSpacing);
  scrollArrows_ = list_scrollbar::forWindow(count, win.first, win.count);
}

void UiGridActivity::loop() {
  // Read each edge ONCE per pass and reuse the answer. A hint-band tap is synthesized by
  // TapStroke, which is CONSUMING: the first wasPressed() for that hardware id spends the
  // tap, so asking a second time further down answered false and the band's Back/Toggle
  // did nothing while the physical keys (non-consuming edge flags) worked.
  const bool confirmPressed = mappedInput.wasPressed(MappedInputManager::Button::Confirm);
  const bool backPressed = mappedInput.wasPressed(MappedInputManager::Button::Back);
  if (handleCustomInput()) return;
  const auto route = UiAppHost::routeTouch(mappedInput);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) return;

  if (confirmPressed) {
    if (selected_ >= 0 && selected_ < cellCount()) activateCell(selected_);
    return;
  }
  if (backPressed) {
    onBackButton();
    return;
  }

  // Rows on the side pair, cells on the front pair, and each press counted once.
  // ScreenUp/ScreenDown/ScreenLeft/ScreenRight rather than the raw buttons, so a
  // rotated screen keeps moving the way the hints under it say it does.
  // A hold ramps the same way a list's does (util/HoldRepeat.h).
  buttonNavigator.onStep({MappedInputManager::Button::ScreenDown}, [this] { moveSelection(1, 0); });
  buttonNavigator.onStep({MappedInputManager::Button::ScreenUp}, [this] { moveSelection(-1, 0); });
  buttonNavigator.onContinuous({MappedInputManager::Button::ScreenDown},
                               [this] { moveSelection(holdRepeatStep(buttonNavigator.repeats()), 0); });
  buttonNavigator.onContinuous({MappedInputManager::Button::ScreenUp},
                               [this] { moveSelection(-holdRepeatStep(buttonNavigator.repeats()), 0); });
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
  const ListChrome bands = shownChrome();
  drawListChromeTop(renderer, bands);
  renderUi();
  if (reservedHeight() > 0) {
    const list_chrome::Bands measured = listChromeBands(renderer, bands);
    drawReserved(Rect{0, measured.contentTop, renderer.getScreenWidth(), reservedHeight()});
  }
  GUI.drawScrollArrows(renderer, scrollArrowBand_, scrollArrows_);
  drawListChromeBottom(renderer, mappedInput, bands);
  if (drawOverlay()) return;
  renderer.displayBuffer();
}
