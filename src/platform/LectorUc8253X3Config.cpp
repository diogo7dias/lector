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
// WHAT THE SWEEP FOUND
// --------------------
// The UC8253 datasheet is not public. The sibling UC8157c datasheet documents R30h as
// [-, -, M[2:0], N[2:0]] with a frame-rate table in which 0x09 is M=1, N=1 = 20 Hz. That
// cannot be right for this panel: 19 frames at 20 Hz is 950 ms, and the device measures
// 626 ms. So the UC8253's table differs from its sibling's.
//
// It differs in which field matters, too. Walking M with N held (0x09, 0x19) was measured
// on the device and produced a bit-identical 566 ms FAST waveform: the high field does
// nothing here. The low 3 bits are the lever. Measured on X3, lector.exp.43:
//
//   pll    FAST waveform   FAST page turn   FULL     HALF      text
//   0x09      566 ms          638 ms        856 ms   2320 ms   clean (driver stock)
//   0x19      566 ms          638 ms        856 ms   2320 ms   clean (no change at all)
//   0x3B      490 ms          563 ms       1022 ms   1985 ms   clean
//   0x3D      428 ms          500 ms        861 ms   2380 ms   clean  <- landed
//   0x3F      352 ms          425 ms        629 ms   1305 ms   grey haze on body text
//
// 0x3F is the first rung that outruns the panel: text leaves a grey haze that the next
// refresh does not clear. 0x3D is the fastest value that still prints clean, 24% off the
// stock waveform, and is what ships. Anything faster needs LUT work, which is out of
// scope. Ghost-free beats fast, so the last clean rung wins, never the fastest one.
//
// HOW A CANDIDATE IS TRIED
// ------------------------
// Write the byte into /perf/pll.txt on the card ("0x3D", "3D" and "61" are all read as
// the same value) and power-cycle. No rebuild, no flash. The value in force is printed
// in the timings overlay and in the CSV header, so a run can never be attributed to the
// wrong candidate. With no such file the landed default below is used.
//
// BEFORE LANDING A VALUE AS THE DEFAULT, all four must hold:
//   1. FAST panel time falls by at least 15% against 0x09.
//   2. Body text is clean after 30 consecutive FAST passes.
//   3. Grayscale still shows four distinct levels. One PLL register scales EVERY bank,
//      including _gc, whose phases are one frame long — a faster clock can collapse the
//      mid-greys. Check this on a cover sleep screen, not on text, or it will not show.
//   4. HALF and FULL still fully clear. They are what the anti-ghost policy relies on.
//
// THE POST-WAVEFORM SETTLE
// ------------------------
// Same shape of unknown, same way of answering it. The driver delays 200 ms after every
// non-differential waveform that does not power the panel down, and that number has never
// been checked against this panel — it is simply what the driver has always sent. It is
// paid on every HALF and every FULL, so it recurs on every clean the anti-ghost policy
// forces, and on the X3 a HALF measures about 2380 ms in total.
//
// Write a millisecond count into /perf/settle.txt to try one; 0 is a legitimate candidate
// and is the end of the sweep worth reaching. Suggested rungs: 200, 100, 50, 0. A value
// lands only if HALF and FULL still fully clear and body text stays clean over 30
// consecutive FAST passes afterwards — the settle exists to leave the panel in a state
// the next differential can build on, so a too-short value shows up as ghosting on the
// passes AFTER the clean, not on the clean itself.

#include <HalStorage.h>
#include <PerfStats.h>

#include <cstdlib>

#include "driver/Uc8253X3Driver.h"
#include "platform/LectorX3Pll.h"

namespace {

// The fastest PLL byte that still prints clean on the X3, and the value in force on any
// device without a /perf/pll.txt. The driver's own stock 0x09 stays the baseline every
// candidate was measured against; see the table above.
constexpr unsigned char kLandedPll = 0x3D;
constexpr const char* kPllPath = "/perf/pll.txt";

// The driver's own settle, and the baseline every candidate is measured against. Never
// verified against this panel: it is simply what the driver has always sent.
constexpr unsigned short kStockSettleMs = 200;
constexpr const char* kSettlePath = "/perf/settle.txt";
// A settle longer than this is a stuck file or a typo, not a candidate. The whole FULL
// waveform is 710 ms, so nothing above it can be a settle.
constexpr unsigned short kMaxSettleMs = 1000;

}  // namespace

unsigned char lectorX3PllByte() {
  // Cached: panel init asks for this, and so does the CSV header written at a completely
  // different point in boot. Reading the card twice could hand back two different values
  // if the card were pulled in between, and a log that disagrees with the panel is worse
  // than no log at all.
  static bool chosen = false;
  static unsigned char active = kLandedPll;
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

unsigned short lectorX3SettleMs() {
  // Cached for the same reason the PLL byte is: the panel config asks for this once, and
  // a value that changed under a running device would make a log disagree with the panel.
  static bool chosen = false;
  static unsigned short active = kStockSettleMs;
  if (chosen) return active;
  chosen = true;

  char buf[16] = {0};
  if (Storage.readFileToBuffer(kSettlePath, buf, sizeof(buf)) > 0) {
    char* end = nullptr;
    const long parsed = strtol(buf, &end, 10);
    // 0 is a legitimate candidate here, unlike the PLL byte: "no settle at all" is exactly
    // the end of the sweep worth reaching.
    if (end != buf && parsed >= 0 && parsed <= kMaxSettleMs) active = static_cast<unsigned short>(parsed);
  }
  return active;
}

namespace freeink {

const Uc8253X3Config& lectorUc8253X3Config() {
  static Uc8253X3Config cfg = uc8253X3DefaultConfig();
  cfg.pll = lectorX3PllByte();
  cfg.postWaveformSettleMs = lectorX3SettleMs();
  return cfg;
}

}  // namespace freeink
