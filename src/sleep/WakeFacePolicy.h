#pragma once
#include <cstdint>

namespace wake_face {
// Persisted IDs must not be renumbered; they are CrossPoint's (see the enum in
// CrossPointSettings.h).
//
// A file this build wrote carries "sleepFaceNumbering": a stored value no build offers
// (a future build's) becomes Custom.
//
// A file without the key is older. Lector's own pre-2026-09-14 numbering differed from
// CrossPoint's at 7 (Stats Dashboard), 8 (Freeze) and 9 (Transparent), and those three were
// retired to Custom on load ever since, so 0.58.0 files hold only 2 or 3 and read the same
// either way. A card that skipped every build since keeps what its numbers meant: 7 and 8
// are Custom, and 9, the transparent overlay, is today's 7.
inline constexpr uint8_t sleepFaceFromFile(const uint8_t stored, const bool numbered) {
  if (numbered) return stored <= 8 ? stored : 2;
  if (stored == 9) return 7;
  return stored <= 6 ? stored : 2;
}

// How long the wake waits, measured from gpio.begin(), before the recovery chord is
// read. The X3/X4 buttons are an ADC resistor ladder that shares a supply with the
// panel rails; the X4 Pro's are debounced digital inputs. Fast Unlock cuts the ladder
// window from 500 ms to 100 ms: the card mount, the settings load and the display
// bring-up already run inside the window, and the chord itself is still confirmed by
// two agreeing samples 6 ms apart. Chosen from code inspection, hence the setting.
inline constexpr unsigned long inputSettleMs(const bool isX4Pro, const bool fastUnlock) {
  if (isX4Pro) return 20;
  return fastUnlock ? 100 : 500;
}

// How a wake from a painted sleep face (a wallpaper, a cover, or the Lector fallback)
// gets the page onto the panel.
//
// Blank: the clearing pass every wake used to run — a FULL request over a blanked
// framebuffer (the SSD1677 promotes it to its HALF anyway), 710 ms on an X3 and
// 1809 ms on an X4, then the destination's own FAST on top. Safe for any content.
//
// DriveAll: no clearing pass. The destination's first FAST is asked to drive EVERY pixel
// toward its target (the controller's old plane is written as the target's complement,
// the mechanism night mode already uses on every driver), so whatever ink the panel
// holds is driven out by the same short waveform that draws the page. One submission,
// ~505 ms on an X4. The UC8253 X3 driver promotes the first paint after begin() to its
// half scrub regardless, so there it is one ~710 ms pass carrying the page; the UC8279 X3
// driver honours the drive-all request itself (Uc8279Driver::requestDriveAllNextFast). What that short
// waveform leaves of a dense wallpaper is the hardware question; the first page turn is
// promoted to a clean pass on the boards that ran the differential (see
// firstPageTurnCleans), so any residue lives on one page only.
//
// Only with Fast Unlock; off restores the clearing pass.
enum class WakeClear : uint8_t { Blank, DriveAll };

inline constexpr WakeClear wakeClearFor(const bool fastUnlock) {
  return fastUnlock ? WakeClear::DriveAll : WakeClear::Blank;
}

// After a DriveAll wake the reader's first page turn runs the periodic clean pass, so
// residue of the sleep face survives at most one page. Not on the X3: its first paint was
// already the half scrub, and an explicit HALF there costs a 2551 ms resync.
inline constexpr bool firstPageTurnCleans(const WakeClear clear, const bool isX3) {
  return clear == WakeClear::DriveAll && !isX3;
}

// Whether a sleep uses Quick Resume (keeps the page + moon, saves the frame for wake).
// Matches CrossPoint's quickResumeSleepScreen: always when Sleep Screen is Quick Resume,
// or on idle-timeout sleep when Quick Resume on Timeout is enabled.
inline constexpr bool usesQuickResumeForSleep(const uint8_t sleepScreen, const uint8_t quickResumeOnTimeout,
                                              const bool fromTimeout) {
  constexpr uint8_t kQuickResumeMode = 6;
  return sleepScreen == kQuickResumeMode || (fromTimeout && quickResumeOnTimeout != 0);
}

inline constexpr bool quickResumePossible(const uint8_t sleepScreen, const uint8_t quickResumeOnTimeout) {
  constexpr uint8_t kQuickResumeMode = 6;
  return sleepScreen == kQuickResumeMode || quickResumeOnTimeout != 0;
}
}  // namespace wake_face
