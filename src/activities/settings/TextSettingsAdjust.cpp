// Text Settings' inline number adjuster: a strip docked under the live preview, so the page
// reflows as the number moves and nothing covers it. It replaced a full-screen slider
// dialog that hid the preview, which made every try "open, guess, OK, look".
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>

#include <string>

#include "MappedInputManager.h"
#include "TextSettingsActivity.h"
#include "components/RowSlider.h"
#include "components/SliderField.h"
#include "components/SliderLook.h"
#include "fontIds.h"

void TextSettingsActivity::openEdit(const Row row) {
  const uint8_t* field = lookField(row);
  if (!field || !settingOf(row)) return;
  editRow_ = row;
  editStart_ = *field;
  stripDragging_ = false;
  strip_ = {};  // laid out by the first paint; nothing to hit before it
  // The list moves down under the strip; a finger still on the row that opened it must not
  // land on the strip's track now sitting where that row was.
  mappedInput.spendTouchContact();
  // The edited row stays the selected one, so the window keeps it in view under the strip.
  const auto rows = visibleRows();
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    if (rows[i] == row) setSelected(i);
  }
  requestUpdate();
}

void TextSettingsActivity::closeEdit() {
  const uint8_t* field = lookField(editRow_);
  // Back where it started: the stored settings already say this, so nothing is written.
  if (field && *field == editStart_) settingsDirty_ = false;
  editRow_ = Row::Count;
  stripDragging_ = false;
  commitSettings();
  requestUpdate();
}

void TextSettingsActivity::stepEdit(const int delta) {
  const uint8_t* field = lookField(editRow_);
  const SettingInfo* info = settingOf(editRow_);
  if (!field || !info) return;
  const slider_field::Range range{info->valueRange.min, info->valueRange.max};
  setEditedValue(editRow_, slider_field::step(*field, range, delta, /*wrap=*/false));
}

// Claims the step keys and the strip's touches; Confirm and Back reach activateCell() and
// onBackButton() through the base, which read them before this runs.
bool TextSettingsActivity::handleEditInput() {
  if (handleStripTouch()) return true;
  const int small = stepOf(editRow_, false);
  const auto side = slider_field::sideDeltas(gpio.deviceIsX3(), stepOf(editRow_, true));
  bool stepped = false;
  const auto step = [this, &stepped](const int delta) {
    stepped = true;
    stepEdit(delta);
  };
  using Button = MappedInputManager::Button;
  buttonNavigator.onPressAndContinuous({Button::Left}, [&] { step(-small); });
  buttonNavigator.onPressAndContinuous({Button::Right}, [&] { step(small); });
  buttonNavigator.onPressAndContinuous({Button::Up}, [&] { step(side.up); });
  buttonNavigator.onPressAndContinuous({Button::Down}, [&] { step(side.down); });
  if (stepped) return true;
  // Between a held key's repeats the list must not move under the strip either.
  for (const Button button : {Button::Left, Button::Right, Button::Up, Button::Down}) {
    if (mappedInput.isPressed(button)) return true;
  }
  return false;
}

// Same contact model as the light panel: minus and plus act once per press, the track
// follows the finger for the whole contact once a drag starts on it, wherever it wanders.
bool TextSettingsActivity::handleStripTouch() {
  const SettingInfo* info = settingOf(editRow_);
  if (!info || !mappedInput.hasTouch()) return false;
  int x = 0;
  int y = 0;
  const bool down = mappedInput.wasScreenTouchDown(x, y);
  const bool held = !down && mappedInput.isScreenTouchHeld(x, y);
  if (!down && !held) {
    stripDragging_ = false;
    return false;
  }
  const row_slider::Bar bar{strip_.track.x, strip_.track.y, strip_.track.width, strip_.track.height};
  const auto dragTo = [&](const int at) {
    setEditedValue(editRow_,
                   row_slider::valueForX(bar, at, info->valueRange.min, info->valueRange.max, stepOf(editRow_, false)));
  };
  if (stripDragging_) {
    // `down` repeats the landing point while the contact is a tap candidate; follow the
    // live position instead.
    int liveX = x;
    int liveY = y;
    mappedInput.isScreenTouchHeld(liveX, liveY);
    dragTo(liveX);
    return true;
  }
  if (!down) return false;
  switch (adjust_strip::hitTest(strip_, x, y)) {
    case adjust_strip::Hit::Minus:
      mappedInput.spendTouchContact();
      stepEdit(-stepOf(editRow_, false));
      return true;
    case adjust_strip::Hit::Plus:
      mappedInput.spendTouchContact();
      stepEdit(stepOf(editRow_, false));
      return true;
    case adjust_strip::Hit::Track:
      stripDragging_ = true;
      dragTo(x);
      return true;
    case adjust_strip::Hit::Strip:
      // The rules and the label are part of the control, not the list behind it.
      mappedInput.spendTouchContact();
      return true;
    case adjust_strip::Hit::None:
      break;
  }
  return false;
}

void TextSettingsActivity::drawStrip(const int y) {
  const uint8_t* field = lookField(editRow_);
  const SettingInfo* info = settingOf(editRow_);
  if (!field || !info) return;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  strip_ = adjust_strip::forStrip(y, renderer.getScreenWidth(), lineHeight);
  const auto& s = strip_;
  renderer.fillRect(s.frame.x, s.frame.y, s.frame.width, adjust_strip::kRule, true);
  renderer.fillRect(s.frame.x, s.frame.y + s.frame.height - adjust_strip::kRule, s.frame.width, adjust_strip::kRule,
                    true);

  const std::string value = rowValueText(editRow_);
  renderer.drawText(UI_10_FONT_ID, s.label.x, s.label.y, I18N.get(rowNameId(editRow_)), true);
  const int valueWidth = renderer.getTextWidth(UI_10_FONT_ID, value.c_str());
  renderer.drawText(UI_10_FONT_ID, s.label.x + s.label.width - valueWidth, s.label.y, value.c_str(), true);

  if (s.track.width > 0) {
    slider_look::drawTrack(renderer, s.track.x, s.track.y, s.track.width, s.track.height, *field - info->valueRange.min,
                           info->valueRange.max - info->valueRange.min, true);
  }
  slider_look::drawStepper(renderer, s.minus.x, s.minus.y, s.minus.width, s.minus.height, /*plus=*/false, true);
  slider_look::drawStepper(renderer, s.plus.x, s.plus.y, s.plus.width, s.plus.height, /*plus=*/true, true);
}
