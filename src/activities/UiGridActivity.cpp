#include "UiGridActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "ListSwipeGesture.h"
#include "MappedInputManager.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/UiRowWrap.h"

namespace fui = freeink::ui;

UiGridActivity::UiGridActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity(name, renderer, mappedInput), UiAppHost(renderer) {}

void UiGridActivity::onEnter() {
  Activity::onEnter();
  buttonNavigator.resetRowTap();
  resetUi();
  app.on(ACTION_CELL, &UiGridActivity::cellTrampoline, this);
  setArmHook(&UiGridActivity::cellArmTrampoline, this);
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

// X4 Pro shares the existing wrapped rows with keys-only boards. Other touch
// boards keep their cells; touch capability alone is not device identity.
settings_grid::Shape UiGridActivity::gridShape() const {
  if (!usesWrappedRows()) {
    settings_grid::Shape shape;
    shape.minCellHeight = std::max(settings_grid::kMinCellHeight, tallestCellHeight());
    return shape;
  }
  const auto& metrics = UITheme::getInstance().getMetrics();
  settings_grid::Shape shape;
  shape.columns = 1;
  shape.sidePad = 0;
  shape.gap = metrics.listRowGap;
  shape.minCellHeight = metrics.listRowHeight;
  shape.stretchToFill = false;
  return shape;
}

bool UiGridActivity::usesWrappedRows() const {
  return settings_grid::usesWrappedRows(mappedInput.hasTouch(), display.profile().isX4Pro);
}

// Rows and cells measure through the same FreeInkUI target they are drawn with, so
// the line count the height is built from is the line count text() will draw.
namespace {
int wrappedLines(const fui::DrawTarget& target, const fui::TextStyle& style, const char* text, const int width) {
  if (text == nullptr || text[0] == '\0' || width <= 0) return 1;
  const int16_t lineHeight = target.lineHeight(style.font);
  if (lineHeight <= 0) return 1;
  const fui::Size size = fui::measureWrappedText(target, text, style, static_cast<int16_t>(width));
  return std::max(1, size.height / lineHeight);
}

// The label style every row and cell measures and draws with: the body face, wrapping
// over as many lines as it needs.
fui::TextStyle wrappingStyle(const fui::ThemeTokens& theme) {
  fui::TextStyle style = theme.bodyText;
  style.maxLines = 8;
  return style;
}
}  // namespace

int UiGridActivity::rowHeightFor(const int index) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const fui::ThemeTokens* theme = sharedUiThemeCell().load(std::memory_order_acquire);
  if (theme == nullptr) return metrics.listRowHeight;
  const fui::TextStyle style = wrappingStyle(*theme);
  const int lineHeight = uiTarget.lineHeight(style.font);
  const int avail = gridPane().width - theme->spaceSm * 2;
  const char* name = cellName(index);
  const char* value = cellValue(index);
  const int nameW = name ? uiTarget.measureText(style.font, name, style).width : 0;
  const int valueW = value ? uiTarget.measureText(style.font, value, style).width : 0;
  constexpr int gap = 10;
  const auto layout = ui_row_wrap::forRow(
      nameW, valueW, avail, gap, lineHeight, std::max(0, metrics.listRowHeight - lineHeight),
      [&](const int width) { return wrappedLines(uiTarget, style, name, width); },
      [&](const int width) { return wrappedLines(uiTarget, style, value, width); });
  return layout.height;
}

int UiGridActivity::tallestCellHeight() const {
  const fui::ThemeTokens* theme = sharedUiThemeCell().load(std::memory_order_acquire);
  if (theme == nullptr) return settings_grid::kMinCellHeight;
  const fui::TextStyle style = wrappingStyle(*theme);
  const int lineHeight = uiTarget.lineHeight(style.font);
  const settings_grid::Layout probe =
      settings_grid::forPane(gridPane().width, 1, cellCount(), 0, settings_grid::Shape{});
  const int width = probe.cellWidth - theme->spaceSm * 2;
  int tallest = 0;
  const int count = cellCount();
  for (int i = 0; i < count; ++i) {
    const int nameLines = wrappedLines(uiTarget, style, cellName(i), width);
    const int valueLines = cellValue(i) != nullptr ? wrappedLines(uiTarget, style, cellValue(i), width) : 0;
    tallest = std::max(tallest, ui_row_wrap::stackedHeight(nameLines, valueLines, lineHeight, 8));
  }
  return tallest;
}

wrapped_list::Window UiGridActivity::keysOnlyWindow() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return wrapped_list::window(cellCount(), selected_, scrollRow_, gridPane().height, metrics.listRowGap,
                              [this](const int index) { return rowHeightFor(index); });
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
    scrollRow_ = usesWrappedRows() ? keysOnlyWindow().first : settings_grid::scrollToShow(gridLayout(), selected_);
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
  scrollRow_ = usesWrappedRows() ? keysOnlyWindow().first : settings_grid::scrollToShow(gridLayout(), selected_);
}

void UiGridActivity::moveSelection(const int deltaRows, const int deltaCells) {
  const int count = cellCount();
  if (count == 0) return;
  setSelected(settings_grid::step(selected_, count, deltaRows, deltaCells, gridLayout().columns));
}

void UiGridActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<UiGridActivity*>(user)->buildScreen(screen);
}

void UiGridActivity::cellArmTrampoline(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<UiGridActivity*>(user);
  if (event.action != ACTION_CELL) return;
  if (event.value < 0 || event.value >= self->cellCount()) return;
  // Assigned rather than routed through setSelected(): the tapped cell is on
  // screen already, so no scroll follows, and setSelected() takes the render
  // lock this dispatch is running under.
  self->selected_ = event.value;
}

void UiGridActivity::cellTrampoline(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<UiGridActivity*>(user);
  if (event.value < 0 || event.value >= self->cellCount()) return;
  self->selected_ = event.value;
  self->activateCell(event.value);
}

void UiGridActivity::buildScreen(UiScreen& screen) {
  const list_chrome::Bands bands = listChromeBands(renderer, chrome());
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(bands.contentTop), 0,
                                      static_cast<int16_t>(renderer.getScreenHeight() - bands.contentBottom), 0});

  const Rect pane = gridPane();
  const int count = cellCount();
  if (usesWrappedRows()) {
    // One column, each row as tall as its text. The window keeps the selection on
    // screen and hands back where it starts, so a later press scrolls from there.
    const wrapped_list::Window win = keysOnlyWindow();
    scrollRow_ = win.first;
    const int gap = UITheme::getInstance().getMetrics().listRowGap;
    int y = pane.y;
    for (int i = win.first; i < win.first + win.count && i < count; ++i) {
      const int height = rowHeightFor(i);
      buildCell(screen, i, settings_grid::Rect{0, y, pane.width, height});
      y += height + gap;
    }
    // Chevrons in the spacing above and below the rows, as every list draws them.
    const int spacing = UITheme::getInstance().getMetrics().verticalSpacing;
    const int reach = std::min(spacing, list_scrollbar::kHeight + list_scrollbar::kGap);
    scrollArrowBand_ = Rect{0, pane.y - reach, pane.width, pane.height + reach * 2};
    scrollArrows_ = list_scrollbar::forWindow(count, win.first, win.count);
    return;
  }
  const settings_grid::Layout layout = gridLayout();
  for (int i = 0; i < count; ++i) {
    const settings_grid::Rect rect = settings_grid::cellAt(layout, pane.y, i);
    if (rect.width == 0) continue;  // scrolled out
    buildCell(screen, i, rect);
  }
  const int spacing = UITheme::getInstance().getMetrics().verticalSpacing;
  const int reach = std::min(spacing, list_scrollbar::kHeight + list_scrollbar::kGap);
  scrollArrowBand_ = Rect{0, pane.y - reach, pane.width, pane.height + reach * 2};
  scrollArrows_ = list_scrollbar::forWindow(layout.totalRows, layout.scrollRow, layout.visibleRows);
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

  if (usesWrappedRows()) {
    buildRow(screen, index, box);
    return;
  }

  fui::ButtonProps props;
  props.action = ACTION_CELL;
  props.value = static_cast<int16_t>(index);
  props.state = selected ? fui::StateChecked : fui::StateNormal;
  props.styles = theme.button;
  props.radius = static_cast<uint8_t>(theme.controlRadius);
  props.minTouchSize = screen.frame().device().minTouchSize;
  screen.button(props, box);

  // One face, one size, both wrapped: the cell's height was taken from the tallest
  // cell on the screen, so every name and value has the lines it needs.
  fui::TextStyle name = wrappingStyle(theme);
  name.align = fui::TextAlign::Center;
  name.inverted = selected;
  fui::TextStyle value = name;

  const int16_t inset = theme.spaceSm;
  const int16_t width = static_cast<int16_t>(box.width - inset * 2);
  const int16_t lineHeight = target.lineHeight(name.font);
  const int16_t nameHeight = static_cast<int16_t>(lineHeight * wrappedLines(target, name, cellName(index), width));
  const int16_t valueHeight =
      cellValue(index) != nullptr
          ? static_cast<int16_t>(lineHeight * wrappedLines(target, value, cellValue(index), width))
          : 0;
  const int16_t top = static_cast<int16_t>(box.y + std::max(0, (box.height - nameHeight - valueHeight) / 2));
  const fui::Rect line{static_cast<int16_t>(box.x + inset), top, width, nameHeight};
  if (cellName(index) != nullptr) target.text(line, cellName(index), name);
  if (cellValue(index) != nullptr) {
    target.text(fui::Rect{line.x, static_cast<int16_t>(top + nameHeight), line.width, valueHeight}, cellValue(index),
                value);
  }
}

// One row of the settings list: the name on the left, its value against the right
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

  // The same measure rowHeightFor() sized the box from, so what is drawn is what was
  // reserved: name and value on one line when they fit, otherwise the name wrapped
  // over the full width and the value right-aligned on its own line(s) under it.
  const int16_t inset = theme.spaceSm;
  const fui::TextStyle style = wrappingStyle(theme);
  const int16_t lineHeight = target.lineHeight(style.font);
  const int16_t width = static_cast<int16_t>(box.width - inset * 2);
  const char* nameText = cellName(index);
  const char* valueText = cellValue(index);
  const int nameW = nameText ? target.measureText(style.font, nameText, style).width : 0;
  const int valueW = valueText ? target.measureText(style.font, valueText, style).width : 0;
  constexpr int gap = 10;
  const auto layout = ui_row_wrap::forRow(
      nameW, valueW, width, gap, lineHeight, 0, [&](const int w) { return wrappedLines(target, style, nameText, w); },
      [&](const int w) { return wrappedLines(target, style, valueText, w); });

  fui::TextStyle name = style;
  name.align = fui::TextAlign::Left;
  name.inverted = selected;
  fui::TextStyle value = style;
  value.align = fui::TextAlign::Right;
  value.inverted = selected;

  const int16_t top = static_cast<int16_t>(box.y + std::max(0, (box.height - layout.height) / 2));
  const int16_t x = static_cast<int16_t>(box.x + inset);
  const int16_t nameH = static_cast<int16_t>(lineHeight * layout.nameLines);
  if (nameText != nullptr) target.text(fui::Rect{x, top, width, nameH}, nameText, name);
  if (valueText != nullptr) {
    const int16_t valueTop = layout.valueBelow ? static_cast<int16_t>(top + nameH) : top;
    const int16_t valueH = static_cast<int16_t>(lineHeight * std::max(1, layout.valueLines));
    target.text(fui::Rect{x, valueTop, width, valueH}, valueText, value);
  }
}

void UiGridActivity::loop() {
  // Read each edge ONCE per pass and reuse the answer. A hint-band tap is synthesized by
  // TapStroke, which is CONSUMING: the first wasPressed() for that hardware id spends the
  // tap, so asking a second time further down answered false and the band's Back/Toggle
  // did nothing while the physical keys (non-consuming edge flags) worked. The grid asked
  // twice — once here to cancel row-tap state, once at the dispatch below.
  const bool confirmPressed = mappedInput.wasPressed(MappedInputManager::Button::Confirm);
  const bool backPressed = mappedInput.wasPressed(MappedInputManager::Button::Back);
  if (confirmPressed || backPressed) {
    buttonNavigator.resetRowTap();
  }
  if (handleCustomInput()) {
    buttonNavigator.resetRowTap();
    return;
  }
  const auto route = UiAppHost::routeTouch(mappedInput);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) {
    buttonNavigator.resetRowTap();
    return;
  }

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
  buttonNavigator.onRowTap(MappedInputManager::Button::ScreenDown, [this](const int rows) { moveSelection(rows, 0); });
  buttonNavigator.onRowTap(MappedInputManager::Button::ScreenUp, [this](const int rows) { moveSelection(-rows, 0); });
  buttonNavigator.onContinuous({MappedInputManager::Button::ScreenDown}, [this] { moveSelection(1, 0); });
  buttonNavigator.onContinuous({MappedInputManager::Button::ScreenUp}, [this] { moveSelection(-1, 0); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::ScreenLeft}, [this] {
    buttonNavigator.resetRowTap();
    moveSelection(0, -1);
  });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::ScreenRight}, [this] {
    buttonNavigator.resetRowTap();
    moveSelection(0, 1);
  });

  // A swipe scrolls the grid by a row.
  switch (mappedInput.wasListScrollSwipe()) {
    case list_swipe::Scroll::PageDown:
      buttonNavigator.resetRowTap();
      moveSelection(1, 0);
      break;
    case list_swipe::Scroll::PageUp:
      buttonNavigator.resetRowTap();
      moveSelection(-1, 0);
      break;
    default:
      break;
  }
}

void UiGridActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const ListChrome bands = chrome();
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
