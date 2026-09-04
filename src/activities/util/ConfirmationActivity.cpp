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
    safeBodyLines = renderer.wrappedText(fontId, body.c_str(), maxWidth, 3, EpdFontFamily::REGULAR);
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
  size_t lineIdx = 0;
  if (!safeHeading.empty() && lineIdx < MAX_LINES) {
    view.lines[lineIdx++] = safeHeading.c_str();
  }
  for (const auto& line : safeBodyLines) {
    if (lineIdx < MAX_LINES) {
      view.lines[lineIdx++] = line.c_str();
    }
  }
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
