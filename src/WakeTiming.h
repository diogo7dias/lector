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
  // The three below split what used to be one "HalReady" stage. On an X3 it measured
  // 430 ms of a 2010 ms wake and was not even printed: it is the FIRST stamp, so every
  // later stage is a delta from it and its own cost never appeared in the readout. That
  // made the second largest item in the wake invisible. Three different things happen in
  // there — the framework's own startup plus Serial, the panic check and system bring-up,
  // and the ADC button ladder coming up — and only the split says which one to attack.
  SysReady = 0,      // HalSystem::begin() returned; everything before it is the prologue
  GpioReady = 1,     // gpio.begin() returned, so the ADC button ladder is powered
  HalReady = 2,      // power manager and clock up, before the SD card
  SdReady = 3,       // Storage.begin() returned
  ConfigReady = 4,   // settings, state, recents, OPDS and presets loaded
  InputSettled = 5,  // power-button verify plus the recovery-mode button settle window
  DisplayReady = 6,  // setupDisplayAndFonts() returned
  // The three below split what used to be one "ban" stage. On an X3 that stage measured
  // 3675 ms of a 4740 ms wake, which named the wake's whole cost and explained none of
  // it: four different things happen in there — a 52 KB frame read off the card, a full
  // plane write to rebuild the panel's differential baseline, the banner drawing itself,
  // and the panel refresh. Only one of them can be worth attacking, and the split is what
  // says which.
  FrameLoaded = 7,       // the saved sleep frame is in the framebuffer (or was absent)
  BaselineRestored = 8,  // the X3 differential baseline has been written back
  BannersDrawn = 9,      // the banners are in the framebuffer, not yet on the panel
  BannersUp = 10,        // the refresh that puts them on the panel has returned
  ActivityUp = 11,       // the routed activity has run onEnter and painted
  Count = 12,
};

// Switches the card read and write on. Off (the default) means loadPrevious() and
// persist() do nothing, so a stable build pays no SD write per wake for numbers nobody
// is going to look at. mark() is always live: it is two stores into a small array, and
// making it conditional would only add a branch.
//
// Call before loadPrevious(), i.e. after the settings have been read.
void setEnabled(bool enabled);

// Stamp `stage` with the current millis(). Safe to call before begin(); calls that arrive
// out of order are kept as-is, since the reader of these numbers wants the raw stamps.
void mark(Stage stage);

// Clear the slate for the wake starting now. Call once, early in setup(), before the
// first mark().
void beginWake();

// Read the previous wake's stamps from the SD card. Call once, right after the card is
// mounted (Stage::SdReady) and before the unlock banners are drawn — the numbers cannot
// be reported before they have been loaded.
//
// The card, not RTC memory: the X3 cuts power to the RTC block on sleep, so nothing
// stored there survives. See the note at the top of the .cpp.
void loadPrevious();

// Write this wake's stamps to the card, for the next wake to display. Call once at the
// end of the wake, after the last mark(). One 22-byte record, one write per wake.
void persist();

// Human-readable breakdown of the PREVIOUS wake, e.g. "pre 190 gpio 82 hal 4 sd 61
// cfg 129 in 310 disp 138 push 710 act 232 = 2010". The leading "pre" is everything
// before the first stamp — the framework's own startup and Serial — which has no stage of
// its own to be a delta from and would otherwise only show up as the gap between the sum
// and the total. Returns an empty string when no previous wake was recorded,
// which is the case on the first boot after a flash.
//
// The buffer is filled rather than returned by value to keep this off the heap during a
// wake. Pass at least 160 bytes.
void formatPrevious(char* out, size_t outLen);

// Like formatPrevious, but NEVER returns an empty string.
//
// formatPrevious falls back to silence when it has nothing to report, and on the banner
// that silence is indistinguishable from "the overlay was never compiled in". This one
// always says something, so an empty-handed wake can be told apart from a missing build
// flag, and the two ways of coming up empty can be told apart from each other:
//
//   "w4 sd 118 cfg 74 in 512 disp 96 ban 410 act 3180 = 4390"  numbers, wake 4
//   "w0 rtc lost m=3f2a91cc"      the RTC magic word did not survive the sleep
//   "w4 no stamps"                RTC survived, but no stage was ever marked
//
// The leading "wN" is a wake counter kept in the same RTC block. If it climbs across
// sleeps, RTC memory is surviving and the fault is in the stamping; if it is always 0,
// the memory itself is being cleared. That single digit decides where to look next.
//
// Pass at least 160 bytes.
void formatDiagnostic(char* out, size_t outLen);

}  // namespace WakeTiming
