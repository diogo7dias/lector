#pragma once

#include <cctype>
#include <cstddef>
#include <string>

// Reading Themes bundle transferred over the Nearby radio (.cpthemes).
// Holds the same JSON schema as /api/presets and /.crosspoint/reader_presets.json.
// Written to STAGING_PATH only for the transfer and deleted at both ends.
// Arduino-free so the filename/staging rules stay host-testable.
namespace preset_bundle {

constexpr const char* FILE_EXTENSION = ".cpthemes";
constexpr const char* FILE_NAME = "themes.cpthemes";
constexpr const char* STAGING_PATH = "/.crosspoint/themes.cpthemes";

inline bool isBundleFilename(const std::string& name) {
  const std::string extension = FILE_EXTENSION;
  if (name.size() <= extension.size()) return false;
  const size_t start = name.size() - extension.size();
  for (size_t i = 0; i < extension.size(); i++) {
    if (std::tolower(static_cast<unsigned char>(name[start + i])) != extension[i]) return false;
  }
  return true;
}

}  // namespace preset_bundle
