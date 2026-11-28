#pragma once

#include <string>

#include "activities/UiStatusActivity.h"

// Sortes 2.0 screen for Unread Roulette (random unread book as a Book Info card
// with Start / Roll Again) and Flashback (random quote from /My Clippings.txt
// with Jump to Book / Roll Again).
class SortesActivity final : public UiStatusActivity {
 public:
  enum class Mode : uint8_t { UnreadRoulette = 0, Flashback = 1 };

  explicit SortesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Mode mode)
      : UiStatusActivity("Sortes", renderer, mappedInput), mode(mode) {}

  void onEnter() override;

 protected:
  StatusView statusView() const override;
  void onChoiceActivated(int index) override;
  void onBackButton() override { finish(); }

 private:
  Mode mode;
  bool hasItem = false;
  std::string heading;
  std::string subtitle;
  std::string bodyText;
  std::string bookPath;

  void roll();
  void rollUnread();
  void rollFlashback();
};
