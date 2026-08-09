#pragma once

// Pure suffix helpers for sleep-wallpaper filenames.
// No Arduino types, no APP_STATE, no Storage — safe for host-side testing.
// FavoriteImage.h re-exports these; FavoriteImage.cpp includes this header.

#include <string>
#include <string_view>

namespace FavoriteImage {

// True when `filename` has a wallpaper extension (.bmp or .pxc,
// case-insensitive). The single source of truth for the supported-extension
// list: the sleep screen's isWallpaperName, the favorite toggle and the bulk
// mover all reach it, so they can never disagree about which files count.
//
// Takes a string_view so the sleep screen's directory walk can test a raw
// name buffer without a heap allocation per file. These helpers only compare
// and copy — nothing here hands the view to a C API.
bool isImageExtension(std::string_view filename);

bool hasFavoriteSuffix(std::string_view filename);
std::string stripFavoriteSuffix(std::string_view filename);
std::string addFavoriteSuffix(std::string_view filename);

// The favorite-toggled twin of `filename` (x.pxc <-> x_F.pxc), or empty when
// the name is not a wallpaper name. A favorite toggle renames the file but must
// not change its identity: the wallpaper index resolves stale records — and
// refuses duplicate appends — through this name.
std::string favoriteCounterpart(std::string_view filename);

// Whole-path form of the two above: returns `path` with its basename carrying the
// _F suffix (favorite = true) or with it stripped (favorite = false). The directory
// part is copied through verbatim, so no path-joining rule can drift between the
// callers. Returns `path` unchanged when the name is not a wallpaper name or is
// already in the requested state.
//
// Exists so the caller that has to know the post-rename path BEFORE the rename runs
// (the background favorite worker) derives it from the same code as the rename itself.
std::string favoritePathFor(std::string_view path, bool favorite);

}  // namespace FavoriteImage
