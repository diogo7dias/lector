#pragma once

#include <cstddef>
#include <cstdint>

#include "FirmwareSwitchAuditLine.h"

// Records which app partition a firmware flash handed the device to, and
// checks on the next boot whether the handover actually happened.
//
// Why: when the bootloader refuses the image it was pointed at, it silently
// boots the other slot instead and leaves otadata untouched. From the user's
// side the update ran to 100%, the device restarted, and the old firmware came
// back, forever, with nothing to go on. This module turns that into a line on
// the SD card the user can read and send.

namespace firmware_flash {

// Remember that the next boot is expected to run the image at `address`
// (`imageSize` bytes). Call right before restarting.
void recordPendingSwitch(uint32_t address, size_t imageSize);

// Compare the pending record against the partition actually running, then
// clear it. When they disagree, log it and append a line to
// /lector-firmware-update.log. Safe to call when no record exists. Requires
// the SD card to be mounted.
void auditPendingSwitch(const char* version);

// Returns true if the bootloader rolled back the most recently attempted firmware
// handover (the intended slot differed from the slot that actually booted).
bool didPreviousSwitchRollBack();

}  // namespace firmware_flash
