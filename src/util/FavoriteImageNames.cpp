// Pure suffix helpers for sleep-wallpaper filenames (.bmp / .pxc).
// Deliberately free of Arduino types, APP_STATE, and Storage so this
// translation unit can be compiled in the host test environment.

#include "FavoriteImageNames.h"

#include <cctype>
#include <string>
#include <string_view>

namespace FavoriteImage {

namespace {

constexpr std::string_view kFavoriteSuffix = "_F";
constexpr size_t kExtLen = 4;  // ".bmp" / ".pxc"

}  // namespace

bool isImageExtension(std::string_view filename) {
  if (filename.size() < kExtLen) return false;
  const std::string_view ext = filename.substr(filename.size() - kExtLen);
  if (ext[0] != '.') return false;
  char lower[kExtLen];
  for (size_t i = 0; i < kExtLen; i++) {
    lower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(ext[i])));
  }
  const std::string_view lowered(lower, kExtLen);
  return lowered == ".bmp" || lowered == ".pxc";
}

bool hasFavoriteSuffix(std::string_view filename) {
  // A bare "_F.bmp" is the suffix and nothing else, so require at least one
  // character of real name before it.
  if (!isImageExtension(filename) || filename.size() <= kExtLen + kFavoriteSuffix.size()) return false;
  const size_t extPos = filename.size() - kExtLen;
  return filename.substr(extPos - kFavoriteSuffix.size(), kFavoriteSuffix.size()) == kFavoriteSuffix;
}

std::string addFavoriteSuffix(std::string_view filename) {
  if (!isImageExtension(filename) || hasFavoriteSuffix(filename)) return std::string(filename);
  const size_t extPos = filename.size() - kExtLen;
  return std::string(filename.substr(0, extPos)) + std::string(kFavoriteSuffix) + std::string(filename.substr(extPos));
}

std::string stripFavoriteSuffix(std::string_view filename) {
  if (!hasFavoriteSuffix(filename)) return std::string(filename);
  const size_t extPos = filename.size() - kExtLen;
  return std::string(filename.substr(0, extPos - kFavoriteSuffix.size())) + std::string(filename.substr(extPos));
}

std::string favoriteCounterpart(std::string_view filename) {
  if (!isImageExtension(filename)) return {};
  return hasFavoriteSuffix(filename) ? stripFavoriteSuffix(filename) : addFavoriteSuffix(filename);
}

std::string favoritePathFor(std::string_view path, const bool favorite) {
  const size_t slashPos = path.find_last_of('/');
  // Keep the separator with the directory half, so a name at the root ("/x.bmp")
  // and a name in a folder ("/sleep/x.bmp") both rejoin by plain concatenation.
  const std::string_view dir = (slashPos == std::string_view::npos) ? std::string_view() : path.substr(0, slashPos + 1);
  const std::string_view name = (slashPos == std::string_view::npos) ? path : path.substr(slashPos + 1);
  const std::string renamed = favorite ? addFavoriteSuffix(name) : stripFavoriteSuffix(name);
  return std::string(dir) + renamed;
}

}  // namespace FavoriteImage
