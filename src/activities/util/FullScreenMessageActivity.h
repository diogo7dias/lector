#pragma once
#include <HalDisplay.h>

#include <string>
#include <utility>

#include "activities/Activity.h"

class FullScreenMessageActivity final : public Activity {
  std::string text;
  HalDisplay::RefreshMode refreshMode;

 public:
  explicit FullScreenMessageActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string text,
                                     const HalDisplay::RefreshMode refreshMode = HalDisplay::FAST_REFRESH)
      : Activity("FullScreenMessage", renderer, mappedInput), text(std::move(text)), refreshMode(refreshMode) {}
  void onEnter() override;
};
