#include "SleepPauseToggle.h"

#include <HalStorage.h>

#include "sleep/SleepWallpaperIndexStore.h"
#include "util/FavoriteImage.h"

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
    destDir = kSleepDir;
    r.toPause = false;
  } else if (path.rfind(kSleepPrefix, 0) == 0) {
    destDir = kSleepPauseDir;
    r.toPause = true;
  } else {
    return r;  // not under either folder
  }

  Storage.mkdir(destDir.c_str());
  const std::string dst = destDir + "/" + baseName(path);
  // SdFat never overwrites on rename, so a same-named file already sitting in the
  // destination makes this fail. Report that instead of pretending it moved.
  if (!Storage.rename(path.c_str(), dst.c_str())) return r;

  FavoriteImage::replacePathReferences(path, dst);
  // A pause toggle moves the file between folders, so the sleep folder's
  // membership really changed — unlike the favorite rename, this must
  // reconcile at the next boot.
  windex::markDirty();
  r.ok = true;
  r.newPath = dst;
  return r;
}

}  // namespace sleep
}  // namespace crosspoint
