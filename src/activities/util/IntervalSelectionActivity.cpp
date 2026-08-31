#include "IntervalSelectionActivity.h"

#include <HalGPIO.h>
#include <I18n.h>

#include <cstdio>
#include <utility>

#include "components/SliderField.h"

namespace {

// Whatever the caller's format string says, or a bare number when it named none.
std::string formatValue(const StrId formatId, const int value) {
  char text[32];
  if (formatId != StrId::STR_NONE_OPT) {
    snprintf(text, sizeof(text), I18N.get(formatId), static_cast<unsigned int>(value));
  } else {
    snprintf(text, sizeof(text), "%d", value);
  }
  return text;
}

// Built from separate label and value strings, rather than by splitting one
// localized sentence, so the layout does not depend on translators preserving a
// hidden separator.
std::string stepLine(const StrId labelId, const StrId formatId, const int step) {
  char line[64];
  snprintf(line, sizeof(line), "%s %s", I18N.get(labelId), formatValue(formatId, step).c_str());
  return line;
}

}  // namespace

void IntervalSelectionActivity::onEnter() {
  UiStatusActivity::onEnter();
  value = slider_field::clamp(value, slider_field::Range{minValue, maxValue});
  // Neither hint changes; only the readout is rebuilt as the value moves.
  smallStepLine = stepLine(StrId::STR_STEP_HINT_FRONT, valueFormatId, smallStep);
  largeStepLine = stepLine(StrId::STR_STEP_HINT_SIDE, valueFormatId, largeStep);
  refreshValueText();
  requestUpdate();
}

void IntervalSelectionActivity::refreshValueText() {
  // A range whose top means "never" or "off" says so instead of showing the
  // number that stands for it.
  if (maxBoundaryLabelId != StrId::STR_NONE_OPT && value == maxValue) {
    valueText = I18N.get(maxBoundaryLabelId);
    return;
  }
  valueText = formatValue(valueFormatId, value);
}

UiStatusActivity::StatusView IntervalSelectionActivity::statusView() const {
  StatusView view;
  view.title = I18N.get(titleId);
  view.showSlider = true;
  // No caption label: the header already says what this screen sets.
  view.sliderValueText = valueText.c_str();
  view.sliderValue = value;
  view.sliderMin = minValue;
  view.sliderMax = maxValue;
  view.lines[0] = smallStepLine.c_str();
  view.lines[1] = largeStepLine.c_str();
  view.confirmHint = tr(STR_SELECT);
  view.thirdHint = "-";
  view.fourthHint = "+";
  return view;
}

void IntervalSelectionActivity::adjustValue(const int delta) {
  value = slider_field::step(value, slider_field::Range{minValue, maxValue}, delta, /*wrap=*/false);
  refreshValueText();
  requestUpdate();
}

void IntervalSelectionActivity::onSliderChanged(const int next) {
  value = slider_field::clamp(next, slider_field::Range{minValue, maxValue});
  refreshValueText();
  requestUpdate();
}

bool IntervalSelectionActivity::handleCustomInput() {
  // Opened from a hold, the release of that same hold must not count as the
  // answer; swallow it and start listening after the button comes back up.
  if (ignoreConfirmRelease) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      ignoreConfirmRelease = false;
      return true;
    }
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      ignoreConfirmRelease = false;
    }
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { adjustValue(-smallStep); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { adjustValue(smallStep); });
  const auto side = slider_field::sideDeltas(gpio.deviceIsX3(), largeStep);
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this, side] { adjustValue(side.up); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this, side] { adjustValue(side.down); });
  return false;
}

void IntervalSelectionActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void IntervalSelectionActivity::onConfirmButton() {
  setResult(IntervalResult{static_cast<uint32_t>(value)});
  finish();
}
