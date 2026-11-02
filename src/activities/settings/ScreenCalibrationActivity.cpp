#include "ScreenCalibrationActivity.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalScreenCalibration.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"

namespace {
// Row order, in the Portrait frame this screen always runs in.
constexpr int gfx::ViewableInsets::* EDGES[] = {&gfx::ViewableInsets::top, &gfx::ViewableInsets::right,
                                                &gfx::ViewableInsets::bottom, &gfx::ViewableInsets::left};
constexpr StrId EDGE_NAMES[] = {StrId::STR_TOP, StrId::STR_DIR_RIGHT, StrId::STR_BOTTOM, StrId::STR_DIR_LEFT};
constexpr int BORDER_WIDTH = 2;
}  // namespace

void ScreenCalibrationActivity::onEnter() {
  UiStatusActivity::onEnter();
  // Top means the top of the panel only upright, and a border is only worth judging there.
  previousOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Portrait);
  draft = renderer.portraitViewableInsets();
  refreshText();
  requestUpdate();
}

void ScreenCalibrationActivity::onExit() {
  renderer.setOrientation(previousOrientation);
  UiStatusActivity::onExit();
}

void ScreenCalibrationActivity::refreshText() {
  for (int i = 0; i < EDGE_COUNT; ++i) {
    char line[32];
    snprintf(line, sizeof(line), "%s: %d", I18N.get(EDGE_NAMES[i]), draft.*EDGES[i]);
    edgeLabels[i] = line;
    rows[i] = edgeLabels[i].c_str();
  }
  rows[ROW_RESET] = tr(STR_CALIBRATION_RESET);
  rows[ROW_SAVE] = tr(STR_CALIBRATION_SAVE);
}

UiStatusActivity::StatusView ScreenCalibrationActivity::statusView() const {
  StatusView view;
  // No header: a black band would hide the top edge of the border, the one most often wrong.
  view.linesAtTop = true;
  const int edge = choiceIndex();
  if (edge < EDGE_COUNT) {
    view.showSlider = true;
    view.sliderLabel = I18N.get(EDGE_NAMES[edge]);
    snprintf(valueText, sizeof(valueText), "%d", draft.*EDGES[edge]);
    view.sliderValueText = valueText;
    view.sliderValue = draft.*EDGES[edge];
    view.sliderMin = 0;
    view.sliderMax = gfx::MAX_CALIBRATED_INSET;
  }
  view.lines[0] = saveFailed ? tr(STR_CALIBRATION_SAVE_FAILED) : tr(STR_CALIBRATION_HELP);
  view.lines[1] = tr(STR_CALIBRATION_HELP_BOOKS);
  view.choiceList = rows.data();
  view.choiceListCount = static_cast<int>(rows.size());
  view.backHint = tr(STR_CANCEL);
  view.confirmHint = tr(STR_SELECT);
  view.thirdHint = "-";
  view.fourthHint = "+";
  return view;
}

bool ScreenCalibrationActivity::drawOverlay() {
  renderer.drawRect(draft.left, draft.top, renderer.getScreenWidth() - draft.left - draft.right,
                    renderer.getScreenHeight() - draft.top - draft.bottom, BORDER_WIDTH, true);
  return false;
}

void ScreenCalibrationActivity::setSelectedEdge(const int value) {
  const int edge = choiceIndex();
  if (edge >= EDGE_COUNT) return;
  draft.*EDGES[edge] = std::clamp(value, 0, gfx::MAX_CALIBRATED_INSET);
  refreshText();
  requestUpdate();
}

bool ScreenCalibrationActivity::handleCustomInput() {
  // Left and Right step the edge; the base would otherwise walk the rows with them.
  const int edge = choiceIndex();
  const int current = edge < EDGE_COUNT ? draft.*EDGES[edge] : 0;
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left},
                                       [this, current] { setSelectedEdge(current - 1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right},
                                       [this, current] { setSelectedEdge(current + 1); });
  return mappedInput.wasPressed(MappedInputManager::Button::Left) ||
         mappedInput.wasPressed(MappedInputManager::Button::Right);
}

void ScreenCalibrationActivity::onSliderChanged(const int value) { setSelectedEdge(value); }

void ScreenCalibrationActivity::onChoiceActivated(const int index) {
  if (index == ROW_RESET) {
    const auto& board = BoardConfig::ACTIVE.viewableInsets;
    draft = {board.top, board.right, board.bottom, board.left};
    saveFailed = false;
    refreshText();
    requestUpdate();
  } else if (index == ROW_SAVE) {
    const uint8_t edges[4] = {static_cast<uint8_t>(draft.top), static_cast<uint8_t>(draft.right),
                              static_cast<uint8_t>(draft.bottom), static_cast<uint8_t>(draft.left)};
    saveFailed = !HalScreenCalibration::save(edges);
    if (saveFailed) {
      requestUpdate();
      return;
    }
    renderer.setViewableInsets(draft);
    finish();
  }
}
