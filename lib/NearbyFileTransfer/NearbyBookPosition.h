#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>

namespace nearby_file {
// The reader's existing little-endian progress record. EPUB includes its content
// offset when available; TXT/MD and fixed-page books use their native page record.
struct BookPosition {
  std::array<uint8_t, 10> bytes = {};
  uint8_t length = 0;

  bool validFor(const std::string& filename) const {
    if (length == 0) return true;
    const auto dot = filename.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = filename.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    if (ext == ".epub") return length == 4 || length == 6 || length == 10;
    return (ext == ".txt" || ext == ".md" || ext == ".xtc" || ext == ".xtch" || ext == ".pxc") && length == 4;
  }
};
}  // namespace nearby_file
