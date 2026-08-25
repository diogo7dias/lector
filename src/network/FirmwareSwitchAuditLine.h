#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace firmware_flash {

// The line appended to the SD-card log when a firmware handover did not
// happen. Pure text, so the host tests can pin the wording.
inline std::string formatSwitchFailedLine(const char* version, uint32_t intendedAddress, uint32_t runningAddress,
                                          size_t imageSize) {
  char line[192];
  std::snprintf(line, sizeof(line),
                "%s: firmware written to 0x%06X (%u bytes) was refused at boot; running 0x%06X instead\n",
                version ? version : "?", static_cast<unsigned>(intendedAddress), static_cast<unsigned>(imageSize),
                static_cast<unsigned>(runningAddress));
  return std::string(line);
}

}  // namespace firmware_flash
