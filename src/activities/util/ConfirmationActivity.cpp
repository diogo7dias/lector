#include "ConfirmationActivity.h"

#include <I18n.h>

ConfirmationActivity::ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const std::string& heading, const std::string& body, const bool cannotUndo)
    : UiStatusActivity("Confirmation", renderer, mappedInput), heading(heading), body(body), cannotUndo(cannotUndo) {}

UiStatusActivity::StatusView ConfirmationActivity::statusView() const {
  StatusView view;
  view.linesAtTop = true;
  // A section wraps its paragraph to the body width, so a long file name or server name
  // reads whole instead of being cut to one line.
  if (!heading.empty()) view.sections[0].heading = heading.c_str();
  if (!body.empty()) {
    view.sections[0].paragraph = body.c_str();
    view.sections[0].paragraphMaxLines = 3;
  }
  view.backHint = tr(STR_CANCEL);
  view.confirmHint = tr(STR_CONFIRM);
  view.cancelLabel = tr(STR_CANCEL);
  view.acceptLabel = tr(STR_CONFIRM);
  view.acceptCannotUndo = cannotUndo;
  return view;
}

void ConfirmationActivity::onConfirmButton() { answer(true); }

void ConfirmationActivity::onBackButton() { answer(false); }

void ConfirmationActivity::answer(const bool yes) {
  ActivityResult res;
  res.isCancelled = !yes;
  setResult(std::move(res));
  finish();
}
