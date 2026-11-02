#pragma once
#include <ViewableInsets.h>

#include <array>
#include <string>

#include "activities/UiStatusActivity.h"
#include "util/ButtonNavigator.h"

// Moves each edge of the page in from the bezel. A border drawn on the draft shows where the
// edges fall; nothing applies until Save, so Back leaves the reader as it was.
class ScreenCalibrationActivity final : public UiStatusActivity {
 public:
  explicit ScreenCalibrationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiStatusActivity("ScreenCalibration", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;

 protected:
  StatusView statusView() const override;
  bool handleCustomInput() override;
  bool drawOverlay() override;
  void onSliderChanged(int value) override;
  void onChoiceActivated(int index) override;

 private:
  static constexpr int EDGE_COUNT = 4;
  static constexpr int ROW_RESET = EDGE_COUNT;
  static constexpr int ROW_SAVE = EDGE_COUNT + 1;

  gfx::ViewableInsets draft;
  GfxRenderer::Orientation previousOrientation = GfxRenderer::Portrait;
  bool saveFailed = false;
  ButtonNavigator buttonNavigator;
  // The view hands these out as pointers, so they outlive it.
  std::array<std::string, EDGE_COUNT> edgeLabels;
  std::array<const char*, ROW_SAVE + 1> rows{};
  mutable char valueText[8] = "";

  void refreshText();
  void setSelectedEdge(int value);
};
