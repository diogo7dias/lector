#include "PopupItemsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>
#include <iterator>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BoundMenuLabels.h"

namespace fui = freeink::ui;

namespace {

constexpr int ITEM_COUNT = static_cast<int>(std::size(CrossPointSettings::POPUP_ITEM_FUNCTIONS));

}  // namespace

void PopupItemsActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
}

int PopupItemsActivity::listCount() const { return ITEM_COUNT; }

void PopupItemsActivity::buildScreen(UiScreen& screen) {
  rows.assign(ITEM_COUNT, fui::ListItem{});
  for (int i = 0; i < ITEM_COUNT; ++i) {
    const uint8_t function = CrossPointSettings::POPUP_ITEM_FUNCTIONS[i];
    rows[i].label = I18N.get(boundMenuActionLabel(function));
    rows[i].value = I18N.get(SETTINGS.isPopupItem(function) ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps props{};
  props.items = rows.data();
  props.count = static_cast<uint16_t>(ITEM_COUNT);
  props.action = ACTION_ROW;
  syncListViewport(screen, props);
  screen.list(props);
}

void PopupItemsActivity::activateIndex(const int index) {
  const uint8_t function = CrossPointSettings::POPUP_ITEM_FUNCTIONS[index];
  const bool wantOn = !SETTINGS.isPopupItem(function);

  if (!SETTINGS.setPopupItem(function, wantOn)) {
    // The only way a toggle is refused is the cap; say so and leave the banner up. No
    // requestUpdate() here on purpose — that would repaint the list straight over it. The
    // next navigation press brings the list back, which is how every transient banner in
    // the firmware behaves on e-ink.
    GUI.drawPopup(renderer, tr(STR_POPUP_FULL));
    return;
  }

  // Ticks are rare and each one is a real user decision, so a save per toggle stays
  // well inside the SPIFFS write budget; setPopupItem() already returned false for a
  // no-op, so an unchanged mask never reaches this line.
  SETTINGS.saveToFile();
  requestUpdate();
}

ListChrome PopupItemsActivity::chrome() const {
  snprintf(counterText, sizeof(counterText), "%u / %u", static_cast<unsigned>(SETTINGS.popupItemCount()),
           static_cast<unsigned>(CrossPointSettings::POPUP_ITEM_MAX));
  ListChrome chrome;
  chrome.title = tr(STR_POPUP_ITEMS);
  chrome.subHeader = counterText;
  chrome.confirmHint = tr(STR_TOGGLE);
  return chrome;
}
