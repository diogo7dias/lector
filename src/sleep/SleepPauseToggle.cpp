#include "SleepPauseToggle.h"

#include <HalStorage.h>

#include "CrossPointState.h"
#include "sleep/SleepPathReferences.h"
#include "sleep/Wallpaper.h"
#include "sleep/WallpaperDirectPickPolicy.h"

namespace crosspoint {
namespace sleep {

namespace {
constexpr const char* kSleepPrefix = "/sleep/";
constexpr const char* kPausePrefix = "/sleep pause/";

std::string baseName(const std::string& p) {
  const auto slash = p.find_last_of('/');
  return (slash == std::string::npos) ? p : p.substr(slash + 1);
}
}  // namespace

bool isUnderSleepDirs(const std::string& path) {
  // Check the pause folder first: its prefix also starts with "/sleep".
  return path.rfind(kPausePrefix, 0) == 0 || path.rfind(kSleepPrefix, 0) == 0;
}

SleepPauseToggleResult toggleSleepPause(const std::string& path) {
  SleepPauseToggleResult r;
  r.newPath = path;

  std::string destDir;
  if (path.rfind(kPausePrefix, 0) == 0) {
    destDir = "/sleep";
    r.toPause = false;
  } else if (path.rfind(kSleepPrefix, 0) == 0) {
    destDir = "/sleep pause";
    r.toPause = true;
  } else {
    return r;  // not under either folder
  }

  Storage.mkdir(destDir.c_str());
  const std::string dst = destDir + "/" + baseName(path);
  if (!Storage.rename(path.c_str(), dst.c_str())) {
    return r;
  }
  replacePathReferences(path, dst);
  // Moving a wallpaper OUT of /sleep means "stop showing this one" — clear the
  // paused-rotation flag so the sleep screen doesn't keep re-showing the file
  // from its new /sleep pause home (the reference fixup above repointed
  // lastSleepWallpaperPath there). Mirrors the reader triage move behavior.
  if (r.toPause && APP_STATE.wallpaperRotationPaused) {
    APP_STATE.wallpaperRotationPaused = false;
    APP_STATE.saveToFile();
  }
  r.ok = true;
  r.newPath = dst;
  if (wallpaper_direct_pick::shouldMarkFolderDirty(r.ok)) wallpaper::markFolderDirty();
  return r;
}

}  // namespace sleep
}  // namespace crosspoint
