#pragma once

#include <ClockZones.h>

#include "activities/UiListActivity.h"

// Time zone selection list (lib/hal/ClockZones.h, CrossPoint's table). Selecting an entry saves it
// as SETTINGS.clockTimezone and returns; every clock reads the setting, so nothing else is pushed.
class TimezonePickerActivity final : public UiListActivity {
 public:
  TimezonePickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("TimezonePicker", renderer, mappedInput) {}

  void onEnter() override;

 protected:
  int listCount() const override { return clock_zones::COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

 private:
  // Members, not statics: they live on the heap only while this screen is open. The row names
  // are the table's own strings; only the standard offsets are formatted here.
  char offsets[clock_zones::COUNT][10];
  freeink::ui::ListItem items[clock_zones::COUNT];
};
