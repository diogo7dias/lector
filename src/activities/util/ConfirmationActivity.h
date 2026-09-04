#pragma once
#include <string>
#include <vector>

#include "activities/UiStatusActivity.h"
#include "components/OptionPopup.h"

// Asks a yes/no question with the shared popup. The question and whatever it is
// about are printed at the top of the body, because the popup itself sits in the
// middle of the screen and would otherwise cover them.
class ConfirmationActivity : public UiStatusActivity {
 public:
  ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& heading,
                       const std::string& body);

  void onEnter() override;

 protected:
  StatusView statusView() const override;
  bool handleCustomInput() override;
  void onBackButton() override;
  bool drawOverlay() override;

 private:
  std::string heading;
  std::string body;

  // Wrapped to the panel width once on entry, so the paint hands out pointers
  // to strings that are already the right length.
  std::string safeHeading;
  std::vector<std::string> safeBodyLines;
  OptionPopup confirmPopup;

  void cancel();
};
