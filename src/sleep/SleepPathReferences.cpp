#include "SleepPathReferences.h"

#include <string>

#include "CrossPointState.h"

namespace crosspoint {
namespace sleep {

namespace {

std::string getBasename(const std::string& path) {
  const auto slashPos = path.find_last_of('/');
  return (slashPos == std::string::npos) ? path : path.substr(slashPos + 1);
}

bool startsWith(const std::string& value, const char* prefix) { return value.rfind(prefix, 0) == 0; }

bool isInSleepFolder(const std::string& path) { return startsWith(path, "/sleep/"); }

}  // namespace

void replacePathReferences(const std::string& oldPath, const std::string& newPath) {
  if (oldPath == newPath) {
    return;
  }
  const bool oldInSleep = isInSleepFolder(oldPath);
  const bool newInSleep = isInSleepFolder(newPath);
  const std::string oldBase = getBasename(oldPath);
  const std::string newBase = getBasename(newPath);

  if (oldInSleep && newInSleep) {
    // The single rotation cursor references a basename; move it with the rename or
    // it is left pointing at the dead pre-rename name.
    if (APP_STATE.lastShownSleepFilename == oldBase) {
      APP_STATE.lastShownSleepFilename = newBase;
    }
  } else if (oldInSleep && !newInSleep) {
    if (APP_STATE.lastShownSleepFilename == oldBase) {
      APP_STATE.lastShownSleepFilename.clear();
    }
  }

  if (APP_STATE.lastSleepWallpaperPath == oldPath) {
    APP_STATE.lastSleepWallpaperPath = newPath;
  }
}

void removePathReferences(const std::string& path) {
  if (APP_STATE.lastSleepWallpaperPath == path) {
    APP_STATE.lastSleepWallpaperPath.clear();
  }
  if (isInSleepFolder(path)) {
    const std::string base = getBasename(path);
    if (APP_STATE.lastShownSleepFilename == base) {
      APP_STATE.lastShownSleepFilename.clear();
    }
  }
}

}  // namespace sleep
}  // namespace crosspoint
