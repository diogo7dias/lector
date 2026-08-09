#include "FavoriteImage.h"

#include <HalStorage.h>

#include <string>

#include "CrossPointState.h"
#include "FavoriteImageNames.h"
#include "sleep/SleepWallpaperIndexStore.h"

namespace FavoriteImage {
namespace {

std::string getBasename(const std::string& path) {
  const auto slashPos = path.find_last_of('/');
  return (slashPos == std::string::npos) ? path : path.substr(slashPos + 1);
}

}  // namespace

// hasFavoriteSuffix / addFavoriteSuffix / stripFavoriteSuffix / isImageExtension
// are defined in FavoriteImageNames.cpp (pure helpers, no Arduino/APP_STATE).

bool isFavoritePath(const std::string& path) { return hasFavoriteSuffix(getBasename(path)); }

std::string displayNameForPath(const std::string& path) {
  const std::string filename = getBasename(path);
  if (!hasFavoriteSuffix(filename)) return filename;
  return std::string("[F] ") + stripFavoriteSuffix(filename);
}

SetFavoriteResult setFavorite(const std::string& path, const bool favorite, std::string* updatedPath) {
  if (!isImageExtension(path)) return SetFavoriteResult::NotImage;
  if (!Storage.exists(path.c_str())) return SetFavoriteResult::Missing;

  std::string currentPath = path;
  // Same helper the background worker uses to predict this path, so the name it
  // advertised to the UI and the name renamed here can never be derived differently.
  const std::string targetPath = favoritePathFor(currentPath, favorite);

  if (targetPath != currentPath) {
    // SdFat never overwrites on rename, so check first and report the clash
    // rather than letting it fail opaquely.
    if (Storage.exists(targetPath.c_str())) return SetFavoriteResult::RenameConflict;
    if (!Storage.rename(currentPath.c_str(), targetPath.c_str())) return SetFavoriteResult::RenameFailed;
    replacePathReferences(currentPath, targetPath);
    currentPath = targetPath;
    // Persist the reference fixup above. There is no favorites list to maintain:
    // the _F suffix on currentPath IS the favorite state now.
    APP_STATE.saveToFile();
  }

  if (updatedPath != nullptr) *updatedPath = currentPath;
  return SetFavoriteResult::Success;
}

void replacePathReferences(const std::string& oldPath, const std::string& newPath) {
  if (oldPath == newPath) return;
  // Deliberately no windex::markDirty() here: the favorite toggle funnels
  // through this and its rename does not change the folder's membership — the
  // index pick resolves the record through favoriteCounterpart(), and the
  // reconcile's counterpart check refuses the duplicate append. Callers that
  // move a file BETWEEN folders (SleepPauseToggle) mark dirty themselves.
  // The wake path re-renders this exact file to composite the unlock banners over
  // it, so a stale path here means the wake falls back to the boot logo.
  if (APP_STATE.lastSleepWallpaperPath == oldPath) APP_STATE.lastSleepWallpaperPath = newPath;
}

void removePathReferences(const std::string& path) {
  // Same funnel as replacePathReferences, for the delete flows.
  crosspoint::sleep::windex::markDirty();
  if (APP_STATE.lastSleepWallpaperPath == path) APP_STATE.lastSleepWallpaperPath.clear();
}

}  // namespace FavoriteImage
