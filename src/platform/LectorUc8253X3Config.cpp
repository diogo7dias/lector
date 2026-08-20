// Lector's UC8253 (Xteink X3) panel config: the stock FreeInk X3 waveform values with a
// tunable PLL byte.
//
// WHY this override exists
// ------------------------
// A page turn on the X3 costs 626 ms of panel time, measured over 80 refreshes. The
// _fast waveform is 19 frames long (two phases, 14 then 5, counted out of the LUT), so
// once the ~48 ms of SPI is subtracted, one frame is taking roughly 30 ms. The frame
// clock is set once at init by the PLL register, R30h, and this driver has always sent
// 0x09. Every frame of every waveform lasts one period of that clock, so it scales the
// duration of every refresh on the device.
//
// WHY THE VALUE IS SWEPT RATHER THAN CHOSEN
// -----------------------------------------
// The UC8253 datasheet is not public. The sibling UC8157c datasheet documents R30h as
// [-, -, M[2:0], N[2:0]] with a frame-rate table in which 0x09 is M=1, N=1 = 20 Hz. That
// cannot be right for this panel: 19 frames at 20 Hz is 950 ms, and the device measures
// 626 ms. So the UC8253's table differs from its sibling's, and reading a value off the
// UC8157 table would be a guess dressed up as a citation.
//
// What does carry across is the structure: the byte is two 3-bit fields, and within the
// UC8157 table, holding N and raising M raises the frame rate monotonically. So the sweep
// holds N=1 and walks M upward from the value in use. If the measured refresh time falls
// as M rises, the structure holds and the fastest value that still prints clean wins. If
// it does not fall, the assumption was wrong and the idea is dead.
//
// HOW A CANDIDATE IS TRIED
// ------------------------
// Write the byte into /perf/pll.txt on the card ("0x19", "19" and "25" are all read as
// the same value) and power-cycle. No rebuild, no flash. The value in force is printed
// in the timings overlay and in the CSV header, so a run can never be attributed to the
// wrong candidate. With no such file the driver's stock 0x09 is used, which is what every
// device that has never been measured on runs.
//
// BEFORE LANDING A VALUE AS THE DEFAULT, all four must hold:
//   1. FAST panel time falls by at least 15% against 0x09.
//   2. Body text is clean after 30 consecutive FAST passes.
//   3. Grayscale still shows four distinct levels. One PLL register scales EVERY bank,
//      including _gc, whose phases are one frame long — a faster clock can collapse the
//      mid-greys. Check this on a cover sleep screen, not on text, or it will not show.
//   4. HALF and FULL still fully clear. They are what the anti-ghost policy relies on.

#include <HalStorage.h>
#include <PerfStats.h>

#include <cstdlib>

#include "driver/Uc8253X3Driver.h"
#include "platform/LectorX3Pll.h"

namespace {

// The driver's own value, and the baseline every candidate is measured against.
constexpr unsigned char kStockPll = 0x09;
constexpr const char* kPllPath = "/perf/pll.txt";

}  // namespace

unsigned char lectorX3PllByte() {
  // Cached: panel init asks for this, and so does the CSV header written at a completely
  // different point in boot. Reading the card twice could hand back two different values
  // if the card were pulled in between, and a log that disagrees with the panel is worse
  // than no log at all.
  static bool chosen = false;
  static unsigned char active = kStockPll;
  if (chosen) return active;
  chosen = true;

  char buf[16] = {0};
  if (Storage.readFileToBuffer(kPllPath, buf, sizeof(buf)) > 0) {
    // Base 0: "0x19" is read as hex, "25" as decimal. Both name the same byte, and both
    // are things a person writing this file by hand will reasonably type.
    char* end = nullptr;
    const long parsed = strtol(buf, &end, 0);
    if (end != buf && parsed > 0 && parsed <= 0xFF) active = static_cast<unsigned char>(parsed);
  }
  PerfStats::setPllByte(active);
  return active;
}

namespace freeink {

const Uc8253X3Config& lectorUc8253X3Config() {
  static Uc8253X3Config cfg = uc8253X3DefaultConfig();
  cfg.pll = lectorX3PllByte();
  return cfg;
}

}  // namespace freeink
