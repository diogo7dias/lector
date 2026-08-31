#include "FontPickerActivity.h"

#include <I18n.h>

#include "fontIds.h"

namespace fui = freeink::ui;

// The family names are drawn in their own scripts, so the list needs the font
// with the widest coverage rather than the UI face.
int FontPickerActivity::listFontId() const { return UI_10_FONT_ID; }

void FontPickerActivity::onEnter() {
  UiListActivity::onEnter();
  moveSelectionTo(currentIndex);
}

void FontPickerActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
}

void FontPickerActivity::buildScreen(UiScreen& screen) {
  const int count = listCount();
  rows.assign(static_cast<size_t>(count), fui::ListItem{});
  for (int i = 0; i < count; ++i) {
    rows[i].label = names[i].c_str();
    // The family in use says so, so the list can be left without choosing and
    // still answer "which one is this".
    if (i == currentIndex) rows[i].value = tr(STR_SELECTED);
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps props{};
  props.items = rows.data();
  props.count = static_cast<uint16_t>(rows.size());
  props.action = ACTION_ROW;
  syncListViewport(screen, props);
  screen.list(props);
}

void FontPickerActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  setResult(IntervalResult{static_cast<uint32_t>(index)});
  finish();
}

void FontPickerActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
