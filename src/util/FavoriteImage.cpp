#include "FavoriteImage.h"

#include <HalStorage.h>

#include <string>

#include "CrossPointState.h"
#include "FavoriteImageNames.h"

namespace FavoriteImage {
namespace {

bool isImagePath(const std::string& path) { return FavoriteImage::isImageExtension(path); }

bool startsWith(const std::string& value, const char* prefix) { return value.rfind(prefix, 0) == 0; }

std::string getBasename(const std::string& path) {
  const auto slashPos = path.find_last_of('/');
  return (slashPos == std::string::npos) ? path : path.substr(slashPos + 1);
}

std::string getParentPath(const std::string& path) {
  const auto slashPos = path.find_last_of('/');
  if (slashPos == std::string::npos || slashPos == 0) {
    return "/";
  }
  return path.substr(0, slashPos);
}

std::string joinPath(const std::string& parent, const std::string& name) {
  if (parent.empty() || parent == "/") {
    return "/" + name;
  }
  return parent + "/" + name;
}

bool isInSleepFolder(const std::string& path) { return startsWith(path, "/sleep/"); }

void updateSleepReferencesOnPathChange(const std::string& oldPath, const std::string& newPath) {
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

void removeSleepReferencesForPath(const std::string& path) {
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

}  // namespace

// hasFavoriteSuffix / addFavoriteSuffix / stripFavoriteSuffix / isImageExtension
// are defined in FavoriteImageNames.cpp (pure helpers, no Arduino/APP_STATE).

bool isFavoritePath(const std::string& path) { return hasFavoriteSuffix(getBasename(path)); }

std::string displayNameForPath(const std::string& path) {
  const std::string filename = getBasename(path);
  if (!isFavoritePath(path)) {
    return filename;
  }
  return std::string("[F] ") + stripFavoriteSuffix(filename);
}

SetFavoriteResult setFavorite(const std::string& path, const bool favorite, std::string* updatedPath) {
  if (!isImagePath(path)) {
    return SetFavoriteResult::NotImage;
  }
  if (!Storage.exists(path.c_str())) {
    return SetFavoriteResult::Missing;
  }

  std::string currentPath = path;
  const std::string currentName = getBasename(currentPath);
  const std::string targetName = favorite ? addFavoriteSuffix(currentName) : stripFavoriteSuffix(currentName);

  if (targetName != currentName) {
    const std::string targetPath = joinPath(getParentPath(currentPath), targetName);
    if (targetPath != currentPath) {
      if (Storage.exists(targetPath.c_str())) {
        return SetFavoriteResult::RenameConflict;
      }
      if (!Storage.rename(currentPath.c_str(), targetPath.c_str())) {
        return SetFavoriteResult::RenameFailed;
      }
      replacePathReferences(currentPath, targetPath);
      currentPath = targetPath;
    }
  }

  // No favoriteBmpPaths list to maintain — the _F suffix on `currentPath` IS the
  // favorite state now. Persist the reference fixups made above.
  APP_STATE.saveToFile();
  if (updatedPath != nullptr) {
    *updatedPath = currentPath;
  }
  return SetFavoriteResult::Success;
}

void replacePathReferences(const std::string& oldPath, const std::string& newPath) {
  if (oldPath == newPath) {
    return;
  }
  updateSleepReferencesOnPathChange(oldPath, newPath);
}

void removePathReferences(const std::string& path) { removeSleepReferencesForPath(path); }

}  // namespace FavoriteImage
