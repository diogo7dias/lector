#pragma once
#include <cstdint>

namespace wake_face {
// Persisted IDs must not be renumbered. Retired Dark, Blank, Quick Resume and
// Freeze selections become Light; all other sleep faces keep their identity.
inline constexpr uint8_t migrateSleepScreen(uint8_t mode) {
  return mode == 0 || mode == 5 || mode == 6 || mode == 8 ? 1 : mode;
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

// How a wake from a painted sleep face clears the panel before the destination paints.
//
// Blank: the clearing pass every wake used to run — a FULL request over a blanked
// framebuffer (the SSD1677 promotes it to its HALF anyway), 710 ms on an X3 and
// 1809 ms on an X4, then the destination's own FAST on top. Required when the face is
// arbitrary content the firmware cannot reconstruct: a wallpaper, a cover, the stats
// dashboard, a transparent overlay.
//
// Differential: no clearing pass. The lock recorded that it painted the crest face, which
// is drawn from flash and deterministic (APP_STATE.lastBootLogo names the crest, the
// pending wake book names the banner). The wake redraws that exact frame into the
// framebuffer, writes it to the controller's "old" plane without a refresh, and lets the
// destination's FAST drive only the pixels that differ — the same differential a page
// turn runs, from a known previous frame. One submission instead of two.
//
// Only with Fast Unlock (the hardware ghosting check for this lives behind the same
// row as the settle cut), only when the lock actually painted the crest, and only
// without the unlock banners: the banner path has its own blocking pass.
enum class WakeClear : uint8_t { Blank, Differential };

inline constexpr WakeClear wakeClearFor(const bool fastUnlock, const bool crestOnGlass, const bool straightToBook) {
  return fastUnlock && crestOnGlass && straightToBook ? WakeClear::Differential : WakeClear::Blank;
}
}  // namespace wake_face
