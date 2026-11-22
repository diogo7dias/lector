#include "UiStatusActivity.h"

// UiStatusActivity's answer shapes: the choice band, the scrolling list and the comparison.
// Split from UiStatusActivity.cpp for the token limit.

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

void UiStatusActivity::buildChoiceBand(UiScreen& screen, const StatusView& view) {
  // A fixed pair (take theirs / send mine) or a list the screen owns (the
  // readers it found): both end up as the same rows, selected by the same
  // index, dispatched through the same action.
  const char* const* labels = view.choices[0] != nullptr ? view.choices.data() : view.choiceList;
  const int offered = view.choices[0] != nullptr ? static_cast<int>(MAX_CHOICES) : view.choiceListCount;

  int count = 0;
  for (int i = 0; i < offered && count < static_cast<int>(MAX_LIST_CHOICES); ++i) {
    if (labels[i] == nullptr || labels[i][0] == '\0') continue;
    ++count;
  }
  choiceCount_ = count;
  if (choiceIndex_ >= count) choiceIndex_ = count > 0 ? count - 1 : 0;
  if (count == 0) return;

  const auto& theme = screen.theme();
  const int16_t gap = theme.listRowGap > 0 ? theme.listRowGap : 4;
  const int16_t rowHeight = theme.rowHeight;

  // Rows are copied into a buffer that outlives the build: FreeInkUI keeps the
  // interaction table pointing at what was drawn.
  for (int i = 0, placed = 0; i < offered && placed < count; ++i) {
    if (labels[i] == nullptr || labels[i][0] == '\0') continue;
    choiceItems_[placed] = fui::ListItem{};
    choiceItems_[placed].label = labels[i];
    choiceItems_[placed].actionValue = static_cast<int16_t>(placed);
    ++placed;
  }

  // Never more than half the body: a long list of readers still leaves the
  // headline that says what the list is for on screen.
  const int16_t bodyHeight = screen.body().height;
  int visible = count;
  const int16_t maxBand = static_cast<int16_t>(bodyHeight / 2);
  while (visible > 1 && rowHeight * visible + gap * (visible - 1) > maxBand) --visible;

  // Scroll only as far as it takes to keep the selection on screen.
  int top = 0;
  if (choiceIndex_ >= visible) top = choiceIndex_ - visible + 1;

  const int16_t band = static_cast<int16_t>(rowHeight * visible + gap * (visible - 1));
  fui::ListProps props;
  props.items = choiceItems_.data();
  props.count = static_cast<uint16_t>(count);
  props.selectedIndex = mappedInput.showsSelection() ? static_cast<int16_t>(choiceIndex_) : -1;
  props.topIndex = static_cast<uint16_t>(top);
  props.action = ACTION_CHOICE;
  props.rowHeight = rowHeight;
  props.rowGap = gap;
  // list() takes the band itself; the spacer after it is the air between the
  // answers and whatever the screen draws above them.
  screen.list(props, band, fui::LayoutAnchor::Bottom);
  screen.spacer(static_cast<int16_t>(gap * 2), fui::LayoutAnchor::Bottom);
}

void UiStatusActivity::buildList(UiScreen& screen, const StatusView& view) {
  const auto& theme = screen.theme();
  auto& target = screen.frame().target();
  listCount_ = view.listCount;
  if (listNav_.selected >= listCount_) listNav_.selected = listCount_ - 1;
  if (listNav_.selected < 0) listNav_.selected = 0;

  fui::ListProps props;
  props.items = view.listItems;
  props.count = static_cast<uint16_t>(listCount_);
  props.action = ACTION_LIST;

  int16_t rowHeight = theme.rowHeight;
  if (!mappedInput.hasTouch()) {
    // Same as UiListActivity: button-only hardware keeps the denser per-theme
    // row so a scan fits as many networks per screen as it did before.
    const auto& metrics = UITheme::getInstance().getMetrics();
    rowHeight = static_cast<int16_t>(view.listHasSubtitle ? metrics.listWithSubtitleRowHeight : metrics.listRowHeight);
    props.rowHeight = rowHeight;
  }
  applyKeysOnlyValueStyle(props, theme);
  applyWrappingRowStyle(props, theme);
  applyInvertedSectionHeaderStyle(props, theme);
  const bool marked = view.listMarks != nullptr;
  if (marked) {
    // One line per row, so every row is rowHeight tall and the marks can be placed
    // without re-deriving a wrap; the layout numbers are named here, not inherited, so
    // the list and drawListMarks() read the same ones. The marks' slot is held back as
    // the value inset (each row carries an empty value).
    props.labelText.maxLines = 1;
    props.rowHeight = rowHeight;
    props.rowGap = theme.listRowGap;
    props.rowInset = theme.listInset;
    props.sidePadding = theme.listSidePadding;
    props.scrollIndicatorWidth = theme.listScrollWidth;
    props.scrollIndicatorInset = theme.listScrollInset;
    props.scrollIndicatorSide = theme.listScrollSide;
    const int chipWidth =
        view.listMarkChip ? target.measureText(theme.smallText.font, view.listMarkChip, theme.smallText).width : 0;
    props.valueInset = static_cast<int16_t>(row_marks::slotWidth(chipWidth));
  }
  const fui::Rect body = screen.body();
  listBand_ = Rect{body.x, body.y, body.width, body.height};
  listNav_.syncToProps(body, rowHeight, theme.listRowGap, listCount_, props);
  if (!mappedInput.showsSelection()) {
    props.selectedIndex = -1;  // see UiListActivity::syncListViewport
    listNav_.followPending = false;
  }
  screen.list(props);
  if (marked) drawListMarks(screen, view, props, body);
}

void UiStatusActivity::drawListMarks(UiScreen& screen, const StatusView& view, const fui::ListProps& props,
                                     const fui::Rect& body) const {
  const auto& theme = screen.theme();
  auto& target = screen.frame().target();
  row_marks::ListLayout layout;
  layout.band = {body.x, body.y, body.width, body.height};
  layout.count = listCount_;
  layout.visible = fui::listVisibleRows(body, props.rowHeight, props.rowGap);
  layout.topIndex = props.topIndex;
  layout.rowHeight = props.rowHeight;
  layout.rowGap = props.rowGap;
  layout.rowInset = props.rowInset;
  layout.sidePadding = props.sidePadding;
  layout.scrollWidth = props.scrollIndicator ? props.scrollIndicatorWidth : 0;
  layout.scrollInset = props.scrollIndicatorInset;
  layout.scrollLeft = props.scrollIndicatorSide == 1;
  const row_marks::RowPlacement place = row_marks::placeRows(layout);
  const int slotRight = place.slotRight;

  // A selected row is inverted (or pilled) by the theme; the marks take its ink.
  fui::StyleSet styles = theme.listRow.unset() ? fui::defaultListRowStyles() : theme.listRow;
  if (theme.listSelectionStyle == fui::SelectionStyle::LightPill) {
    styles.selected.foreground = fui::Paint::solid(fui::Color::Black);
  } else if (theme.listSelectionStyle != fui::SelectionStyle::InvertFill) {
    styles.selected = styles.normal;
  }
  const int chipText =
      view.listMarkChip ? target.measureText(theme.smallText.font, view.listMarkChip, theme.smallText).width : 0;
  const int lineHeight = target.lineHeight(theme.smallText.font);
  const auto fill = [&target](const row_marks::Rect& r, const fui::Paint& ink) {
    target.fill(fui::Rect{static_cast<int16_t>(r.x), static_cast<int16_t>(r.y), static_cast<int16_t>(r.width),
                          static_cast<int16_t>(r.height)},
                ink);
  };

  for (int i = place.top; i < place.end; ++i) {
    const row_marks::Marks& marks = view.listMarks[i];
    const int centreY = place.rowY(i) + props.rowHeight / 2;
    const fui::Paint ink = (props.selectedIndex == i ? styles.selected : styles.normal).foreground;
    if (marks.bars >= 0) {
      const signal_meter::Rect icon = row_marks::barsRect(slotRight, centreY);
      for (int bar = 0; bar < signal_meter::kBarCount; ++bar) {
        const signal_meter::Rect r = signal_meter::barRect(icon, bar);
        if (signal_meter::barIsFilled(bar, marks.bars)) {
          fill(r, ink);
        } else {
          // An outline in fills: the empty bars still show the scale.
          fill({r.x, r.y, r.width, 1}, ink);
          fill({r.x, r.y + r.height - 1, r.width, 1}, ink);
          fill({r.x, r.y, 1, r.height}, ink);
          fill({r.x + r.width - 1, r.y, 1, r.height}, ink);
        }
      }
    }
    if (marks.locked) {
      const row_marks::Rect lock = row_marks::lockRect(slotRight, centreY);
      for (int part = 0; part < row_marks::kLockParts; ++part) fill(row_marks::lockPart(lock, part), ink);
    }
    if (marks.saved && view.listMarkChip) {
      const row_marks::Rect chip = row_marks::chipRect(slotRight, centreY, chipText, lineHeight);
      const fui::Rect box{static_cast<int16_t>(chip.x), static_cast<int16_t>(chip.y), static_cast<int16_t>(chip.width),
                          static_cast<int16_t>(chip.height)};
      target.stroke(box, ink, row_marks::kStroke, 0);
      fui::TextStyle chipStyle = fui::textStyleWithForeground(theme.smallText, ink);
      chipStyle.align = fui::TextAlign::Center;
      target.text(fui::Rect{box.x, static_cast<int16_t>(box.y + 1), box.width, static_cast<int16_t>(lineHeight)},
                  view.listMarkChip, chipStyle);
    }
  }
}

void UiStatusActivity::buildComparison(UiScreen& screen, const StatusView& view) {
  const auto& theme = screen.theme();
  auto& target = screen.frame().target();

  const int16_t headlineHeight = target.lineHeight(theme.bodyText.font);
  const int16_t lineHeight = target.lineHeight(theme.smallText.font);
  const int16_t gap = theme.listRowGap > 0 ? theme.listRowGap : 4;

  comparison_layout::Content content;
  content.hasHeadline = view.comparisonHeadline != nullptr && view.comparisonHeadline[0] != '\0';
  content.hasRelation = view.comparisonRelation != nullptr && view.comparisonRelation[0] != '\0';
  for (size_t side = 0; side < view.comparison.size(); ++side) {
    for (const char* line : view.comparison[side].lines) {
      if (line != nullptr && line[0] != '\0') ++content.sideLines[side];
    }
  }
  const comparison_layout::Metrics stack{headlineHeight, headlineHeight, lineHeight, gap};

  const fui::Rect body = screen.body();
  int16_t y = static_cast<int16_t>(comparison_layout::topFor(stack, body.y, body.height, content));

  if (content.hasHeadline) {
    fui::TextStyle style = theme.bodyText;
    style.align = fui::TextAlign::Center;
    target.text(fui::Rect{body.x, y, body.width, headlineHeight}, view.comparisonHeadline, style);
    y = static_cast<int16_t>(y + headlineHeight);
  }

  for (size_t side = 0; side < view.comparison.size(); ++side) {
    const ComparisonSide& block = view.comparison[side];
    if (block.label == nullptr || block.label[0] == '\0') continue;
    if (y > body.y) y = static_cast<int16_t>(y + gap * 2);
    fui::TextStyle label = theme.bodyText;
    label.align = fui::TextAlign::Center;
    label.color = fui::Color::White;
    target.fill(fui::Rect{body.x, y, body.width, headlineHeight}, fui::Paint::solid(fui::Color::Black));
    target.text(fui::Rect{body.x, y, body.width, headlineHeight}, block.label, label);
    y = static_cast<int16_t>(y + headlineHeight);
    for (const char* line : block.lines) {
      if (line == nullptr || line[0] == '\0') continue;
      y = static_cast<int16_t>(y + gap);
      fui::TextStyle style = theme.smallText;
      style.align = fui::TextAlign::Left;
      target.text(fui::Rect{body.x, y, body.width, lineHeight}, line, style);
      y = static_cast<int16_t>(y + lineHeight);
    }
  }

  if (content.hasRelation) {
    y = static_cast<int16_t>(y + gap * 2);
    fui::TextStyle style = theme.smallText;
    style.align = fui::TextAlign::Left;
    target.text(fui::Rect{body.x, y, body.width, lineHeight}, view.comparisonRelation, style);
  }
}
