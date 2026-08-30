#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// The font family list, opened from Text Settings. Its own activity rather than a
// mode inside that screen: a mode there meant a second selection, a second scroll
// row and a second full-screen paint living inside a grid screen.
//
// Answers with the chosen index in an IntervalResult, or cancelled.
class FontPickerActivity final : public UiListActivity {
 public:
  FontPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::vector<std::string> names,
                     int currentIndex)
      : UiListActivity("FontPicker", renderer, mappedInput), names(std::move(names)), currentIndex(currentIndex) {}

  void onEnter() override;
  void onExit() override;

 protected:
  int listCount() const override { return static_cast<int>(names.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  const char* headerTitle() const override { return tr(STR_FONT); }
  int listFontId() const override;

 private:
  std::vector<std::string> names;
  int currentIndex = 0;
  std::vector<freeink::ui::ListItem> rows;
};
