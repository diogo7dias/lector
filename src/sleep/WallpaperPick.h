#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <string>

namespace crosspoint {
namespace sleep {

// Which wallpaper in `dir` (the folder at `sleepDir`) goes up tonight: the paused hold,
// then the index line, then the jump pick, then the full uniform walk. Empty `chosen`
// when the folder holds none. linePosition/lineTotal are 0/0 unless the index picked.
struct WallpaperPick {
  std::string chosen;
  bool pickedFromIndex = false;
  uint32_t linePosition = 0;
  uint32_t lineTotal = 0;
};

WallpaperPick pickWallpaper(HalFile& dir, const char* sleepDir, const std::string& previousWallpaper);

// Random wallpaper name, or empty when the jump could not resolve one (caller falls
// back to the full walk). `dir` is left at an arbitrary position.
std::string pickWallpaperByJump(HalFile& dir);

}  // namespace sleep
}  // namespace crosspoint
