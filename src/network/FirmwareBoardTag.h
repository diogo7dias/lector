#pragma once

#include <cstddef>
#include <cstdint>

// Board-identity tag embedded in every Lector image, plus a streaming scanner the firmware
// update paths use to reject an image built for a different board before it can boot and drive
// another device's pins. The X4 Pro (ESP32-S3) and the X3/X4 (ESP32-C3) already differ by
// chip_id; the tag is the second line, and it keeps an image from another S3 board out.
//
// The tag is "CROSSPOINT-BOARD-V1:<board>;", the same bytes CrossPoint embeds, so each project's
// guard also recognises the other's images. It lives once in .rodata: the scanner's needle is
// the same array, so an image holds exactly one occurrence. Images with no tag (other projects,
// older releases) are allowed; only a tag naming a DIFFERENT board is refused.

namespace board_tag {

// Full tag: magic prefix + board name + ';'.
extern const char TAG[];

// Board name of the running firmware (a pointer into TAG, not null-terminated at the name:
// always pair with boardNameLen()).
const char* boardName();
size_t boardNameLen();

// Incremental scanner: feed every byte of a candidate image in stream order, then read
// mismatch(). State persists across feed() calls, so a tag split over chunks is still found.
class Scanner {
 public:
  void feed(const uint8_t* data, size_t len);
  bool mismatch() const { return mismatchFound; }
  // Board name from the offending tag, for logging (empty until mismatch()).
  const char* foundName() const { return mismatchFound ? captured : ""; }

 private:
  static constexpr size_t MAX_NAME = 23;
  char captured[MAX_NAME + 1] = {0};
  size_t nameLen = 0;
  size_t magicMatched = 0;
  bool capturing = false;
  bool mismatchFound = false;
};

}  // namespace board_tag
