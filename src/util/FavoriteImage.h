#pragma once

#include <string>

// Pure suffix helpers (hasFavoriteSuffix, addFavoriteSuffix, stripFavoriteSuffix,
// isImageExtension) live in FavoriteImageNames.h / .cpp so they can be compiled
// host-side without Arduino / APP_STATE / Storage.
#include "FavoriteImageNames.h"

// Suffix-only favorites model: a sleep wallpaper is "favorite" iff its basename
// carries the _F suffix (x_F.bmp). There is no separate favorites list and no
// favorites cap — the filename IS the state, so it survives reboots, SD edits
// and card swaps with zero bookkeeping, and nothing can drift out of sync.
namespace FavoriteImage {

enum class SetFavoriteResult {
  Success,
  NotImage,
  Missing,
  RenameConflict,
  RenameFailed,
};

// True iff the path's basename has the _F favorite suffix.
bool isFavoritePath(const std::string& path);

// Human-facing name: "[F] name" for favorites (suffix stripped), otherwise the
// bare basename.
std::string displayNameForPath(const std::string& path);

// Toggle favorite by renaming the file (add/strip _F). Updates the APP_STATE
// sleep reference if it pointed at the renamed file. Returns Success, or a
// failure reason. `updatedPath` (optional) receives the post-rename path.
SetFavoriteResult setFavorite(const std::string& path, bool favorite, std::string* updatedPath = nullptr);

// Fix up the APP_STATE sleep reference when a wallpaper file is renamed/moved.
void replacePathReferences(const std::string& oldPath, const std::string& newPath);

// Clear the APP_STATE sleep reference when a wallpaper file is deleted.
void removePathReferences(const std::string& path);

}  // namespace FavoriteImage
