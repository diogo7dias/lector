#pragma once
// The chapter parser only asks the book for image bytes. Each image streams a PNG header
// whose size comes from the path, so the probe, scaling and placement paths all run.
#include <Print.h>

#include <cstdint>
#include <string>

class Epub {
 public:
  bool readItemContentsToStream(const std::string& href, Print& out, size_t, bool = false) {
    if (href.size() < 4 || href.compare(href.size() - 4, 4, ".png") != 0) return false;
    const uint32_t w = 1 + (href.size() * 97) % 2400;
    const uint32_t h = 1 + (href.size() * 61) % 3200;
    uint8_t png[24] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
    for (int b = 0; b < 4; b++) {
      png[16 + b] = static_cast<uint8_t>(w >> (24 - 8 * b));
      png[20 + b] = static_cast<uint8_t>(h >> (24 - 8 * b));
    }
    out.write(png, sizeof(png));
    return true;
  }
};
