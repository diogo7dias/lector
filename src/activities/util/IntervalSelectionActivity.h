#pragma once

#include <I18n.h>

#include <string>

#include "MappedInputManager.h"
#include "activities/UiStatusActivity.h"
#include "util/ButtonNavigator.h"

class GfxRenderer;

// One number on a slider: every timeout, every count, every interval, and every
// numeric settings row comes through here. A dedicated screen rather than a band
// over the list it was opened from, so the readout, the track, the -/+ step
// buttons and (on touch) Cancel/OK all get room to be finger-sized.
class IntervalSelectionActivity final : public UiStatusActivity {
 public:
  explicit IntervalSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* activityName,
                                     StrId titleId, int initialValue, int minValue, int maxValue, int smallStep,
                                     int largeStep, StrId valueFormatId = StrId::STR_NONE_OPT,
                                     bool readerActivity = false, bool ignoreInitialConfirmRelease = false,
                                     StrId maxBoundaryLabelId = StrId::STR_NONE_OPT, uint8_t minutesPerStep = 0)
      : UiStatusActivity(activityName, renderer, mappedInput),
        titleId(titleId),
        valueFormatId(valueFormatId),
        maxBoundaryLabelId(maxBoundaryLabelId),
        value(initialValue),
        minValue(minValue),
        maxValue(maxValue),
        smallStep(smallStep),
        largeStep(largeStep),
        minutesPerStep(minutesPerStep),
        readerActivity(readerActivity),
        ignoreConfirmRelease(ignoreInitialConfirmRelease) {}

  void onEnter() override;
  bool isReaderActivity() const override { return readerActivity; }

 protected:
  StatusView statusView() const override;
  bool handleCustomInput() override;
  void onBackButton() override;
  void onConfirmButton() override;
  void onSliderChanged(int value) override;
  int sliderStep() const override { return smallStep; }

 private:
  StrId titleId;
  StrId valueFormatId;
  StrId maxBoundaryLabelId;
  int value;
  int minValue;
  int maxValue;
  int smallStep;
  int largeStep;
  uint8_t minutesPerStep;
  bool readerActivity;
  bool ignoreConfirmRelease;
  ButtonNavigator buttonNavigator;

  // The strings the view hands out as pointers, so they outlive it.
  std::string valueText;
  std::string smallStepLine;
  std::string largeStepLine;
  void refreshValueText();

  void adjustValue(int delta);
};
