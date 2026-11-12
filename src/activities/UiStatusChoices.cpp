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
  props.selectedIndex = static_cast<int16_t>(choiceIndex_);
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

  if (view.listNote != nullptr && view.listNote[0] != '\0') {
    // Taken first, so the list is measured for the room actually left.
    const int16_t noteHeight = target.lineHeight(theme.smallText.font);
    const fui::Rect note = screen.takeBottom(noteHeight, theme.spaceSm);
    fui::TextStyle style = theme.smallText;
    style.align = fui::TextAlign::Center;
    target.text(note, view.listNote, style);
  }

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
  const fui::Rect body = screen.body();
  listBand_ = Rect{body.x, body.y, body.width, body.height};
  listNav_.syncToProps(body, rowHeight, theme.listRowGap, listCount_, props);
  screen.list(props);
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
