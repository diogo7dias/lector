#pragma once

#include <string>

namespace large_folder_load {
inline constexpr bool shouldCancel(const bool backPressed, const bool backReleased) {
  return backPressed || backReleased;
}

inline std::string restoredParentPath(std::string path) {
  if (path.size() > 1 && path.back() == '/') path.pop_back();
  return path;
}
}  // namespace large_folder_load
