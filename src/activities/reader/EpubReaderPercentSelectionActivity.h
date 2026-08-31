#pragma once

#include <string>

#include "MappedInputManager.h"
#include "activities/UiStatusActivity.h"
#include "util/ButtonNavigator.h"

// Jump to a place in the book by percent. The value wraps rather than clamps:
// a book has no end to run into, so going up from 99% reaches the front cover.
class EpubReaderPercentSelectionActivity final : public UiStatusActivity {
 public:
  explicit EpubReaderPercentSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                              const int initialPercent)
      : UiStatusActivity("EpubReaderPercentSelection", renderer, mappedInput), percent(initialPercent) {}

  void onEnter() override;

 protected:
  StatusView statusView() const override;
  bool handleCustomInput() override;
  void onBackButton() override;
  void onConfirmButton() override;
  void onSliderChanged(int value) override;
  int sliderStep() const override;

 private:
  int percent = 0;

  ButtonNavigator buttonNavigator;

  // The strings the view hands out as pointers, so they outlive it.
  std::string valueText;
  std::string smallStepLine;
  std::string largeStepLine;
  void refreshText();

  void adjustPercent(int delta);
};
