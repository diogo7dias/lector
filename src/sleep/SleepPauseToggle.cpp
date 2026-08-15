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
  // Measured before the rename, while the source still exists: a move out of
  // the indexed folder is one exact snapshot delta.
  const auto pendingDelete = windex::planDeletion(path);
  // SdFat never overwrites on rename, so a same-named file already sitting in the
  // destination makes this fail. Report that instead of pretending it moved.
  if (!Storage.rename(path.c_str(), dst.c_str())) return r;

  FavoriteImage::replacePathReferences(path, dst);
  // A pause toggle moves the file between folders, so the sleep folder's
  // membership really changed. Both directions are single-entry patches: out of
  // /sleep leaves a hole the pick skips, back into /sleep appends a record that
  // jumps the queue. Neither needs a folder walk at the next boot.
  if (r.toPause) {
    windex::commitDeletion(pendingDelete);
    if (!pendingDelete.valid) windex::markDirty();
  } else {
    windex::noteCreated(dst);
  }
  r.ok = true;
  r.newPath = dst;
  return r;
}

}  // namespace sleep
}  // namespace crosspoint
