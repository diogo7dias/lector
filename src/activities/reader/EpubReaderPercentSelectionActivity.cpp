#include "EpubReaderPercentSelectionActivity.h"

#include <HalGPIO.h>
#include <I18n.h>

#include <cstdio>

#include "components/SliderField.h"

namespace {
// Fine/coarse slider step sizes for percent adjustments.
constexpr int kSmallStep = 1;
constexpr int kLargeStep = 10;
constexpr slider_field::Range kRange{0, 100};

std::string stepLine(const StrId labelId, const int step) {
  char line[64];
  snprintf(line, sizeof(line), "%s %d%%", I18N.get(labelId), step);
  return line;
}
}  // namespace

void EpubReaderPercentSelectionActivity::onEnter() {
  UiStatusActivity::onEnter();
  // Built once: neither hint changes, and the readout is rebuilt on every move.
  smallStepLine = stepLine(StrId::STR_STEP_HINT_FRONT, kSmallStep);
  largeStepLine = stepLine(StrId::STR_STEP_HINT_SIDE, kLargeStep);
  refreshText();
  requestUpdate();
}

void EpubReaderPercentSelectionActivity::refreshText() { valueText = std::to_string(percent) + "%"; }

UiStatusActivity::StatusView EpubReaderPercentSelectionActivity::statusView() const {
  StatusView view;
  view.title = tr(STR_GO_TO_PERCENT);
  view.showSlider = true;
  // No caption label: the header already says what this screen sets, so the
  // caption line carries only the readout.
  view.sliderValueText = valueText.c_str();
  view.sliderValue = percent;
  view.sliderMin = kRange.min;
  view.sliderMax = kRange.max;
  view.lines[0] = smallStepLine.c_str();
  view.lines[1] = largeStepLine.c_str();
  view.confirmHint = tr(STR_SELECT);
  view.thirdHint = "-";
  view.fourthHint = "+";
  return view;
}

int EpubReaderPercentSelectionActivity::sliderStep() const { return kSmallStep; }

void EpubReaderPercentSelectionActivity::adjustPercent(const int delta) {
  percent = slider_field::step(percent, kRange, delta, /*wrap=*/true);
  refreshText();
  requestUpdate();
}

void EpubReaderPercentSelectionActivity::onSliderChanged(const int value) {
  // A drag lands on a value rather than stepping, so it does not wrap.
  percent = slider_field::clamp(value, kRange);
  refreshText();
  requestUpdate();
}

bool EpubReaderPercentSelectionActivity::handleCustomInput() {
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { adjustPercent(-kSmallStep); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { adjustPercent(kSmallStep); });
  const auto side = slider_field::sideDeltas(gpio.deviceIsX3(), kLargeStep);
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this, side] { adjustPercent(side.up); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this, side] { adjustPercent(side.down); });
  return false;
}

void EpubReaderPercentSelectionActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void EpubReaderPercentSelectionActivity::onConfirmButton() {
  setResult(PercentResult{percent});
  finish();
}
