#pragma once

#include <ArduinoJson.h>

#include <string>
#include <string_view>

#include "LutLabLuts.h"
#include "sleep/WallpaperNames.h"

namespace lutlab {
inline bool validWallpaper(std::string_view path) {
  if (path == "/sleep.bmp" || path == "/sleep.pxc") return true;
  const size_t prefix = path.rfind("/sleep/", 0) == 0 ? 7 : path.rfind("/.sleep/", 0) == 0 ? 8 : 0;
  if (!prefix) return false;
  const auto name = path.substr(prefix);
  return name.size() <= 255 && name.find('/') == std::string_view::npos && name.find('\\') == std::string_view::npos &&
         crosspoint::sleep::isWallpaperName(name);
}

struct State {
  uint8_t variant = 0;
  bool pinned = false;
  std::string wallpaper;
  bool imageFailed = false;

  void toJson(JsonObject doc) const {
    doc["variant"] = variant;
    doc["pinned"] = pinned;
    doc["wallpaper"] = wallpaper;
    doc["imageFailed"] = imageFailed;
  }
  void fromJson(JsonVariantConst doc) {
    variant = validVariant(doc["variant"] | 0);
    const char* path = doc["wallpaper"] | "";
    wallpaper = validWallpaper(path) ? path : "";
    pinned = (doc["pinned"] | false) && !wallpaper.empty();
    imageFailed = doc["imageFailed"] | false;
  }
};
}  // namespace lutlab
