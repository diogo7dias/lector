#pragma once

#include <cstddef>
#include <cstdint>

// Writes what the running firmware knows about this device's flash layout and
// boot record to /lector-flash-diagnostics.txt on the SD card.
//
// Why a file: a USB-locked reader gives no serial console, so the only way to
// see why a firmware install "completed" and then came back to Lector is to
// have the firmware that performed the install write down what it did. The
// device that cannot be debugged over a cable can still hand over a text file
// through the web file browser.
//
// What it captures, per attempt: the whole partition table, which slot is
// running, both otadata records before and after the switch, the sequence
// number chosen and why, and the result of validating the image. Appended, one
// block per attempt, so a user can try twice and send one file.

namespace firmware_flash {

// Called before the boot record is touched. `imagePath` may be null for an
// over-the-air install.
void diagnosticsBeginAttempt(const char* version, const char* imagePath, size_t imageSize);

// Called when an install attempt ends before the boot record is touched, so a
// failure leaves a record too. `stage` names where it stopped ("validate",
// "write", "readback"); `result` is firmware_flash::resultName of the failure.
void diagnosticsFailAttempt(const char* stage, const char* result);

// Called after ota_boot::switchTo, with whether it reported success. The
// destination is passed as plain values so this header stays free of ESP
// types.
void diagnosticsEndAttempt(uint32_t destAddress, const char* destLabel, uint8_t destSubtype, bool switchOk);

// Called early on the next boot: records which slot actually came up, which is
// the answer the whole file exists to give.
void diagnosticsRecordBoot(const char* version);

}  // namespace firmware_flash
