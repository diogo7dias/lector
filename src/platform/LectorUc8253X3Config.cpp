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
// as M rises, the structure holds and the fastest value that still prints clean text
// wins. If it does not fall, the assumption was wrong and the idea is dead — which is
// worth knowing in one flash rather than three.
//
// The sweep exists ONLY in the measurement build. Without PERF_LOG this file compiles to
// the stock config with the stock PLL byte, so the stable firmware carries no sweep and
// no behaviour change at all.

#include "PerfLogSink.h"
#include "driver/Uc8253X3Driver.h"

namespace freeink {

const Uc8253X3Config& lectorUc8253X3Config() {
  static Uc8253X3Config cfg = uc8253X3DefaultConfig();
  cfg.pll = lectorX3PllByte();
  return cfg;
}

}  // namespace freeink
