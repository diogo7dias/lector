#include "TimezonePickerActivity.h"

#include <I18n.h>

#include "CrossPointSettings.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

void TimezonePickerActivity::onEnter() {
  UiListActivity::onEnter();
  for (uint8_t i = 0; i < clock_zones::COUNT; i++) {
    clock_zones::formatOffset(i, offsets[i], sizeof(offsets[i]));
    items[i] = fui::ListItem{};
    items[i].label = clock_zones::TABLE[i].name;
    items[i].value = offsets[i];
    items[i].actionValue = static_cast<int16_t>(i);
  }
  // Open on the active zone rather than the top of a 117-row list.
  moveSelectionTo(clock_zones::activeIndex(SETTINGS.clockTimezone));
}

const char* TimezonePickerActivity::headerTitle() const { return tr(STR_TIMEZONE); }

void TimezonePickerActivity::buildScreen(UiScreen& screen) {
  fui::ListProps props{};
  props.items = items;
  props.count = clock_zones::COUNT;
  props.action = ACTION_ROW;
  syncListViewport(screen, props);
  screen.list(props);
}

void TimezonePickerActivity::activateIndex(const int index) {
  if (index < 0 || index >= clock_zones::COUNT) return;
  SETTINGS.clockTimezone = static_cast<uint8_t>(index);
  SETTINGS.saveToFile();
  app.clearTapFlash();
  finish();
}
