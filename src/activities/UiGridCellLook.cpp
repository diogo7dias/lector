// UiGridActivity's two non-row looks: the section heading and the framed card.
#include <algorithm>

#include "MappedInputManager.h"
#include "UiGridActivity.h"
#include "activities/UiGridText.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;
using ui_grid_text::paintsSelected;
using ui_grid_text::wrappedLines;
using ui_grid_text::wrappingStyle;

namespace {
// The house stroke (BaseTheme kLine) and the air inside and under a card. A card is one
// tile of the menu look: 2 px frame, square corners, its gap below so the window, which
// lays rows edge to edge, still leaves air between them.
constexpr int kCardRule = 2;
constexpr int kCardPadX = 10;
constexpr int kCardPadY = 6;
constexpr int kCardGap = 8;
// How far the heading's black band reaches past its label, as the list headings do
// (FreeInkUI ListProps::headerFillPadX).
constexpr int kHeaderPadX = 8;

int cardTextWidth(const int boxWidth) {
  const int side = UITheme::getInstance().getMetrics().listSidePadding;
  return boxWidth - side * 2 - (kCardRule + kCardPadX) * 2;
}
}  // namespace

int UiGridActivity::landableFrom(int index, const int direction) const {
  const int count = cellCount();
  for (; index >= 0 && index < count; index += direction) {
    if (!isHeaderCell(index)) return index;
  }
  return -1;
}

int UiGridActivity::cardHeightFor(const int index) const {
  const fui::ThemeTokens* theme = sharedUiThemeCell().load(std::memory_order_acquire);
  const int lineHeight = theme ? uiTarget.lineHeight(theme->bodyText.font) : 25;
  int lines = 2;
  if (theme != nullptr) {
    const fui::TextStyle style = wrappingStyle(*theme);
    const int width = cardTextWidth(gridPane().width);
    lines = wrappedLines(uiTarget, style, cellName(index), width) +
            (cellValue(index) != nullptr ? wrappedLines(uiTarget, style, cellValue(index), width) : 0);
  }
  return (kCardRule + kCardPadY) * 2 + lines * lineHeight + kCardGap;
}

void UiGridActivity::buildHeader(UiScreen& screen, const int index, const fui::Rect& box) {
  const char* label = cellName(index);
  if (label == nullptr || label[0] == '\0') return;
  auto& target = screen.frame().target();
  fui::TextStyle style = screen.theme().bodyText;
  style.align = fui::TextAlign::Left;
  style.color = fui::Color::White;
  const int side = UITheme::getInstance().getMetrics().listSidePadding;
  const int textW = target.measureText(style.font, label, style).width;
  const int bandX = std::max<int>(box.x, box.x + side - kHeaderPadX);
  const int bandW = std::min<int>(textW + kHeaderPadX * 2, box.width - (bandX - box.x));
  target.fill(fui::Rect{static_cast<int16_t>(bandX), box.y, static_cast<int16_t>(bandW), box.height},
              fui::Paint::solid(fui::Color::Black));
  const int16_t lineHeight = target.lineHeight(style.font);
  const int16_t top = static_cast<int16_t>(box.y + std::max(0, (box.height - lineHeight) / 2));
  target.text(
      fui::Rect{static_cast<int16_t>(box.x + side), top, static_cast<int16_t>(box.width - side * 2), lineHeight}, label,
      style);
}

void UiGridActivity::buildCard(UiScreen& screen, const int index, const fui::Rect& box) {
  const auto& theme = screen.theme();
  auto& target = screen.frame().target();
  const int side = UITheme::getInstance().getMetrics().listSidePadding;
  const bool isSelected = index == selected() && mappedInput.showsSelection();
  const fui::Rect card{static_cast<int16_t>(box.x + side), box.y, static_cast<int16_t>(box.width - side * 2),
                       static_cast<int16_t>(box.height - kCardGap)};

  fui::ButtonProps props;
  props.action = ACTION_CELL;
  props.value = static_cast<int16_t>(index);
  props.state = isSelected ? fui::StateChecked : fui::StateNormal;
  props.styles = fui::plainStyles();
  props.styles.active = theme.listRow.active;
  props.styles.selected.background = fui::Paint::solid(fui::Color::Black);
  props.styles.selected.foreground = fui::Paint::solid(fui::Color::White);
  props.minTouchSize = 0;
  screen.button(props, card);
  target.stroke(card, fui::Paint::solid(fui::Color::Black), kCardRule);

  // The name, then its summary under it, both left-aligned and wrapped to the frame.
  fui::TextStyle style = wrappingStyle(theme);
  style.align = fui::TextAlign::Left;
  style.inverted = isSelected && paintsSelected(screen.frame().stateFor(ACTION_CELL, index));
  const int16_t lineHeight = target.lineHeight(style.font);
  const int16_t width = static_cast<int16_t>(cardTextWidth(box.width));
  const int16_t x = static_cast<int16_t>(card.x + kCardRule + kCardPadX);
  int16_t y = static_cast<int16_t>(card.y + kCardRule + kCardPadY);
  for (const char* text : {cellName(index), cellValue(index)}) {
    if (text == nullptr) continue;
    const int16_t height = static_cast<int16_t>(lineHeight * wrappedLines(target, style, text, width));
    target.text(fui::Rect{x, y, width, height}, text, style);
    y = static_cast<int16_t>(y + height);
  }
}
