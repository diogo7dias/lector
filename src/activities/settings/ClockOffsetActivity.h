#pragma once

#include <GfxRenderer.h>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

// Dedicated UTC offset picker for the status bar clock.
// Three editable fields (sign, hours, minutes); Confirm cycles fields, Up/Down adjust the active one.
// Supports the full IANA UTC offset range in 15 minute steps, including oddball zones like Nepal (+5:45).
// The three fields are FreeInkUI buttons, so their look comes from the theme
// and a touch can reach them: a tap moves the edit to a field, and a tap on the
// field already being edited steps it, which is the only way to change the
// offset without the direction keys.
class ClockOffsetActivity final : public Activity, protected UiAppHost {
 public:
  explicit ClockOffsetActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ClockOffset", renderer, mappedInput), UiAppHost(renderer) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // One action for all three fields; the field is the action's value.
  static constexpr freeink::ui::ActionId ACTION_FIELD = 1;

  void buildScreen(UiScreen& screen);
  static void screenTrampoline(UiScreen& screen, void* user);
  static void fieldTrampoline(const freeink::ui::ActionEvent& event, void* user);
  void onFieldTapped(int field);

  ButtonNavigator buttonNavigator;

  enum Field { FIELD_SIGN = 0, FIELD_HOURS = 1, FIELD_MINUTES = 2, FIELD_COUNT };
  Field activeField = FIELD_HOURS;

  // Working copy of the offset, edited in-place. Saved back to SETTINGS on exit.
  // 0 = positive offset, 1 = negative offset.
  uint8_t sign = 0;
  // Hours: 0..14 when positive, 0..12 when negative.
  uint8_t hours = 0;
  // Quarter-hour index 0..3 (0, 15, 30, 45).
  uint8_t minutesQuarter = 0;

  // The field text, held rather than built in the paint: the buttons are drawn
  // from these pointers.
  char signText[2] = {'+', '\0'};
  char hoursText[8] = {'0', '\0'};
  char minutesText[8] = {'0', '0', '\0'};
  void refreshFieldText();

  /** The live wall-clock preview, empty when the clock cannot say. */
  std::string previewLine;
  void refreshPreview();

  void loadFromSettings();
  void saveToSettings() const;
  void adjustActiveField(int delta);
  void clampForSign();
};
