#pragma once

#include <cstddef>
#include <cstdint>

// Where the seconds go on a wake.
//
// The device has no serial console in the user's hands, so the only way to learn what a
// real wake costs is to measure it on the device and show the numbers on the device. The
// stamps below are written during setup() into RTC_NOINIT memory, which survives deep
// sleep, and are read back and drawn on the *next* wake's unlock banner. One wake behind
// is the point: the last stamp cannot be taken until the reader has painted, which is
// after the banner for that same wake is long gone.
//
// Cost is two 7-entry uint16_t arrays plus a magic word in RTC slow memory, 36 bytes in
// total, written once per wake. Nothing is allocated and nothing is written to the SD card.
namespace WakeTiming {

// Ordered milestones. Each is stamped as milliseconds since boot, so a stage's cost is
// the difference between neighbours. Keep in sync with kStageNames in the .cpp.
enum class Stage : uint8_t {
  HalReady = 0,      // gpio/power/clock up, before the SD card
  SdReady = 1,       // Storage.begin() returned
  ConfigReady = 2,   // settings, state, recents, OPDS and presets loaded
  InputSettled = 3,  // power-button verify plus the recovery-mode button settle window
  DisplayReady = 4,  // setupDisplayAndFonts() returned
  BannersUp = 5,     // the unlock banners are on the panel
  ActivityUp = 6,    // the routed activity has run onEnter and painted
  Count = 7,
};

// Stamp `stage` with the current millis(). Safe to call before begin(); calls that arrive
// out of order are kept as-is, since the reader of these numbers wants the raw stamps.
void mark(Stage stage);

// Copy this wake's stamps into the "previous wake" slot and clear the current ones. Call
// once, early in setup(), before the first mark() of the new wake.
void beginWake();

// Human-readable breakdown of the PREVIOUS wake, e.g. "sd 118 cfg 74 in 512 disp 96
// ban 410 act 3180 = 4390". Returns an empty string when no previous wake was recorded,
// which is the case on the first boot after a flash.
//
// The buffer is filled rather than returned by value to keep this off the heap during a
// wake. Pass at least 96 bytes.
void formatPrevious(char* out, size_t outLen);

}  // namespace WakeTiming
