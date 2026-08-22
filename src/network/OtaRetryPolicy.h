#pragma once

#include <cstddef>
#include <cstdint>

// Decisions a firmware install makes when an attempt ends badly, kept free of
// Arduino, TLS and esp_ota headers so they can be reasoned about (and tested)
// on their own. A 5 MB image over a marginal link drops often enough that one
// attempt is not a fair test of whether the update can be had at all.
namespace ota_retry {

// A whole-image restart on a link that just failed at 80% tends to fail again
// in the same place, so attempts resume rather than restart, and three of them
// is enough to ride out a stall without holding the reader on the screen for
// minutes.
constexpr int MAX_ATTEMPTS = 3;

// Why an attempt ended. Only one of these is the link's fault.
enum class Failure {
  DOWNLOAD,     // the transfer stopped early: dropped connection, TLS reset, timeout
  FLASH_WRITE,  // esp_ota_write refused the bytes
  WRONG_CHIP,   // the image is built for another MCU
};

// Retry the link, never the verdict: a wrong-chip image stays wrong however
// many times it is fetched, and a partition that refused bytes refuses the same
// bytes again. attemptsMade counts the attempt that just failed, so the first
// failure arrives as 1.
inline bool shouldRetry(const Failure failure, const int attemptsMade) {
  return failure == Failure::DOWNLOAD && attemptsMade < MAX_ATTEMPTS;
}

// Wait between attempts, so a router still finishing its own recovery is not
// asked again the same instant.
inline unsigned long backoffMs(const int attemptsMade) {
  return static_cast<unsigned long>(attemptsMade) * 1000UL;
}

// esp_ota_write appends, so bytes already written stay written and the next
// attempt asks the server for the rest. Nothing written means nothing to resume.
inline size_t resumeOffset(const size_t bytesWritten) { return bytesWritten; }

// The image header carries its chip id at offset 12. runningPartitionChipId()
// reports 0xFFFF when it cannot read the device's own id, and an unknown device
// cannot contradict an image.
inline bool isWrongChip(const uint16_t imageChip, const uint16_t deviceChip) {
  return deviceChip != 0xFFFF && imageChip != deviceChip;
}

}  // namespace ota_retry
