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

}  // namespace FavoriteImage
