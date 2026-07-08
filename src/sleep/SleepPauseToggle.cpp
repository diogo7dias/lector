#include "SleepPauseToggle.h"

#include <HalStorage.h>

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
  FavoriteImage::replacePathReferences(path, dst);
  r.ok = true;
  r.newPath = dst;
  return r;
}

}  // namespace sleep
}  // namespace crosspoint
