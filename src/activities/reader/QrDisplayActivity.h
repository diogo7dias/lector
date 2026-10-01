#pragma once
#include <I18n.h>

#include <string>

#include "activities/UiStatusActivity.h"

// A payload as a code, filling the panel: the only thing this screen does is
// give a phone something to point at, so the square takes the whole body.
class QrDisplayActivity final : public UiStatusActivity {
 public:
  explicit QrDisplayActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& textPayload)
      : UiStatusActivity("QrDisplay", renderer, mappedInput), textPayload(textPayload) {}

 protected:
  StatusView statusView() const override;
  void onConfirmButton() override { finish(); }

 private:
  std::string textPayload;
};
