#include "FastPageTurnsTestActivity.h"

#include <HalDisplay.h>
#include <I18n.h>

#include "CrossPointSettings.h"

extern HalDisplay display;

UiStatusActivity::StatusView FastPageTurnsTestActivity::statusView() const {
  StatusView view;
  view.title = tr(STR_FAST_PAGE_TURNS);
  view.headerRight = SETTINGS.fastPageTurns ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  if (display.profile().controllerIsUc8279) {
    view.sections[0].heading = tr(STR_FAST_TURNS_NO_EFFECT_TITLE);
    view.sections[0].paragraph = tr(STR_FAST_TURNS_NO_EFFECT_BODY);
    return view;
  }
  view.sections[0].heading = tr(STR_FAST_TURNS_HOW_TITLE);
  view.sections[0].paragraph = tr(STR_FAST_TURNS_HOW_BODY);
  view.sections[1].heading = tr(STR_FAST_TURNS_LOOK_TITLE);
  view.sections[1].paragraph = tr(STR_FAST_TURNS_LOOK_BODY);
  view.sections[2].heading = tr(STR_FAST_TURNS_REPORT_TITLE);
  view.sections[2].paragraph = tr(STR_FAST_TURNS_REPORT_BODY);
  for (auto& section : view.sections) section.paragraphMaxLines = 6;
  view.confirmHint = tr(STR_TOGGLE);
  view.acceptLabel = SETTINGS.fastPageTurns ? tr(STR_FAST_TURNS_TURN_OFF) : tr(STR_FAST_TURNS_TURN_ON);
  return view;
}

void FastPageTurnsTestActivity::onConfirmButton() {
  if (display.profile().controllerIsUc8279) return;
  SETTINGS.fastPageTurns = SETTINGS.fastPageTurns ? 0 : 1;
  requestUpdate();
}
