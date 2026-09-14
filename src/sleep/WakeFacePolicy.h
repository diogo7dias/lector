#pragma once
#include <cstdint>

namespace wake_face {
// Persisted IDs must not be renumbered. Retired Dark, Light (the crest), Blank, Quick
// Resume and Freeze selections become Custom (a wallpaper); all other sleep faces keep
// their identity.
inline constexpr uint8_t migrateSleepScreen(uint8_t mode) {
  return mode == 0 || mode == 1 || mode == 5 || mode == 6 || mode == 8 ? 2 : mode;
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

// How a wake from a painted sleep face (a wallpaper, a cover, the Lector fallback, the
// stats dashboard, a transparent overlay: all arbitrary content) gets the page onto the
// panel.
//
// Blank: the clearing pass every wake used to run — a FULL request over a blanked
// framebuffer (the SSD1677 promotes it to its HALF anyway), 710 ms on an X3 and
// 1809 ms on an X4, then the destination's own FAST on top. Safe for any content.
//
// DriveAll: no clearing pass. The destination's first FAST is asked to drive EVERY pixel
// toward its target (the controller's old plane is written as the target's complement,
// the mechanism night mode already uses on every driver), so whatever ink the panel
// holds is driven out by the same short waveform that draws the page. One submission,
// ~505 ms on an X4. The X3 driver promotes the first paint after begin() to its half
// scrub regardless, so there it is one ~710 ms pass carrying the page. What that short
// waveform leaves of a dense wallpaper is the hardware question; the first page turn is
// promoted to a clean pass on the boards that ran the differential (see
// firstPageTurnCleans), so any residue lives on one page only.
//
// Only with Fast Unlock (off restores the clearing pass) and only without the unlock
// banners: the banner path has its own blocking pass.
enum class WakeClear : uint8_t { Blank, DriveAll };

inline constexpr WakeClear wakeClearFor(const bool fastUnlock, const bool straightToBook) {
  return fastUnlock && straightToBook ? WakeClear::DriveAll : WakeClear::Blank;
}

// After a DriveAll wake the reader's first page turn runs the periodic clean pass, so
// residue of the sleep face survives at most one page. Not on the X3: its first paint was
// already the half scrub, and an explicit HALF there costs a 2551 ms resync.
inline constexpr bool firstPageTurnCleans(const WakeClear clear, const bool isX3) {
  return clear == WakeClear::DriveAll && !isX3;
}
}  // namespace wake_face
