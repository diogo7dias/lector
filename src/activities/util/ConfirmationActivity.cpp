#include "ConfirmationActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "components/UIScale.h"

ConfirmationActivity::ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const std::string& heading, const std::string& body)
    : UiStatusActivity("Confirmation", renderer, mappedInput), heading(heading), body(body) {}

void ConfirmationActivity::onEnter() {
  UiStatusActivity::onEnter();

  const int fontId = uiScaleSpec().smallFontId;
  const int maxWidth = renderer.getScreenWidth() - 40;
  if (!heading.empty()) {
    safeHeading = renderer.truncatedText(fontId, heading.c_str(), maxWidth, EpdFontFamily::REGULAR);
  }
  if (!body.empty()) {
    safeBody = renderer.truncatedText(fontId, body.c_str(), maxWidth, EpdFontFamily::REGULAR);
  }

  const char* options[] = {I18N.get(StrId::STR_CANCEL), I18N.get(StrId::STR_CONFIRM)};
  confirmPopup.show(safeHeading.c_str(), options, 2, 0, [this](const int choice) {
    ActivityResult res;
    res.isCancelled = (choice != 1);
    setResult(std::move(res));
    finish();
  });

  requestUpdate(true);
}

UiStatusActivity::StatusView ConfirmationActivity::statusView() const {
  StatusView view;
  view.linesAtTop = true;
  if (!safeHeading.empty()) view.lines[0] = safeHeading.c_str();
  if (!safeBody.empty()) view.lines[safeHeading.empty() ? 0 : 1] = safeBody.c_str();
  // The popup carries both answers, so the hint band says nothing.
  view.backHint = "";
  return view;
}

bool ConfirmationActivity::handleCustomInput() {
  if (confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  // The popup went away without an answer (Back, or a tap outside): that is a no.
  cancel();
  return true;
}

void ConfirmationActivity::onBackButton() { cancel(); }

void ConfirmationActivity::cancel() {
  ActivityResult res;
  res.isCancelled = true;
  setResult(std::move(res));
  finish();
}

bool ConfirmationActivity::drawOverlay() { return confirmPopup.processRender(renderer, mappedInput); }
