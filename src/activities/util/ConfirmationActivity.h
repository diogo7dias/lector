#pragma once
#include <string>

#include "activities/UiStatusActivity.h"

// Asks a yes/no question once. A touch board answers with the two buttons at the foot of
// the body; a keys-only board with Back and Confirm, which the band names. The question
// and what it is about sit at the top of the body.
class ConfirmationActivity : public UiStatusActivity {
 public:
  // cannotUndo: the yes answer deletes or overwrites something, so on a touch board its
  // button takes a confirming second tap (components/TwoTapGate.h). Off only for a
  // question whose yes is harmless.
  ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& heading,
                       const std::string& body, bool cannotUndo = true);

 protected:
  StatusView statusView() const override;
  void onConfirmButton() override;
  void onBackButton() override;

 private:
  std::string heading;
  std::string body;
  bool cannotUndo;

  void answer(bool yes);
};
