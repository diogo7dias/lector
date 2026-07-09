#pragma once
#include <string>
#include <utility>

#include "activities/Activity.h"

class BootActivity final : public Activity {
 public:
  // wallpaperPath: when set to a .pxc path, the boot screen re-renders that
  // wallpaper through the grayscale pipeline and composites the unlock banners on
  // top (wallpaper stays on unlock). Empty -> plain white banner screen (cold
  // boot / logo modes / non-pxc wallpaper).
  explicit BootActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string wallpaperPath = {})
      : Activity("Boot", renderer, mappedInput), wallpaperPath_(std::move(wallpaperPath)) {}
  void onEnter() override;

 private:
  std::string wallpaperPath_;

  // Draws the two unlock banners (top: version + resuming book; bottom: 4-block
  // loader). Invoked once per grayscale pass over a wallpaper, and once on the
  // plain white banner screen. Follows the grayscale-overlay rule: the black
  // banner fill is drawn in every pass, white content only in the BW base pass.
  void drawUnlockBanners() const;
};
