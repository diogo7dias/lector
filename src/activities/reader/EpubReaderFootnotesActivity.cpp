#include "EpubReaderFootnotesActivity.h"

#include <I18n.h>

#include "MappedInputManager.h"

namespace fui = freeink::ui;

void EpubReaderFootnotesActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
}

ListChrome EpubReaderFootnotesActivity::chrome() const {
  ListChrome chrome;
  chrome.title = tr(STR_FOOTNOTES);
  // Nothing to open and nothing to move between when the page carries no links.
  const bool hasRows = listCount() > 0;
  chrome.confirmHint = hasRows ? tr(STR_SELECT) : "";
  chrome.thirdHint = hasRows ? tr(STR_DIR_UP) : "";
  chrome.fourthHint = hasRows ? tr(STR_DIR_DOWN) : "";
  return chrome;
}

void EpubReaderFootnotesActivity::buildScreen(UiScreen& screen) {
  const int count = listCount();
  if (count == 0) {
    screen.centeredText(tr(STR_NO_FOOTNOTES));
    return;
  }

  rows.assign(static_cast<size_t>(count), fui::ListItem{});
  for (int i = 0; i < count; ++i) {
    rows[i].label = footnotes[i].number[0] != '\0' ? footnotes[i].number : tr(STR_LINK);
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps props{};
  props.items = rows.data();
  props.count = static_cast<uint16_t>(rows.size());
  props.action = ACTION_ROW;
  syncListViewport(screen, props);
  screen.list(props);
}

void EpubReaderFootnotesActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  setResult(FootnoteResult{footnotes[index].href});
  finish();
}

bool EpubReaderFootnotesActivity::handleButtons() {
  // Power opens a footnote too: on the X3 it is the only key within reach of the
  // hand already holding the device.
  if (mappedInput.wasPressed(MappedInputManager::Button::Power)) {
    activateIndex(nav.selected);
    return true;
  }
  return UiListActivity::handleButtons();
}

void EpubReaderFootnotesActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
