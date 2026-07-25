#pragma once
#include <string>
#include <utility>

#include "activities/Activity.h"

class BootActivity final : public Activity {
 public:
  // wallpaperPath: when set to a .pxc path, the boot screen re-renders that wallpaper
  // and composites the unlock banners on top, so an unlock keeps the wallpaper that was
  // showing during sleep. Empty -> the plain logo boot screen (cold boot, logo/cover/
  // dark sleep screens, or a wallpaper we cannot re-render).
  explicit BootActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string wallpaperPath = {})
      : Activity("Boot", renderer, mappedInput), wallpaperPath_(std::move(wallpaperPath)) {}
  void onEnter() override;

 private:
  std::string wallpaperPath_;
};
