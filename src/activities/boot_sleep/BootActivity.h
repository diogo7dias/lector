#pragma once
#include <string>
#include <utility>

#include "activities/Activity.h"

class BootActivity final : public Activity {
 public:
  // A .pxc path redraws the wallpaper without banners. An empty or unreadable path
  // falls back to the centred name on a white screen.
  explicit BootActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string wallpaperPath = {})
      : Activity("Boot", renderer, mappedInput), wallpaperPath_(std::move(wallpaperPath)) {}
  void onEnter() override;

 private:
  std::string wallpaperPath_;
};
