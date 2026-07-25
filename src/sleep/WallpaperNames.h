#pragma once

#include <string_view>

#include "util/FavoriteImageNames.h"

namespace crosspoint {
namespace sleep {

// A sleep wallpaper is any non-dotfile with a wallpaper extension. The extension
// list itself lives in FavoriteImageNames, so the sleep screen's random pick, the
// favorite toggle and the bulk mover all answer "is this a wallpaper?" the same
// way. A filter that drifts between them would either move files the sleep screen
// ignores or leave files it still shows.
//
// Allocation-free: the sleep screen tests up to a few hundred raw name buffers
// per pick, and a std::string per file there is pure heap churn.
inline bool isWallpaperName(std::string_view name) {
  if (name.empty() || name.front() == '.') return false;
  return FavoriteImage::isImageExtension(name);
}

}  // namespace sleep
}  // namespace crosspoint
