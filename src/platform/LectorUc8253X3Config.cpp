// Lector's UC8253 (Xteink X3) panel config.
//
// The panel now runs exactly what every sibling firmware runs: the SDK's stock X3 driver,
// its stock 0x09 PLL frame clock, and its stock 200 ms post-waveform settle. The tuned
// values and the extra refresh sequencing this file used to carry were reverted on
// 2026-08-21. The measurements are in docs/perf-measurement.md in the git history (before
// the Performance Timings removal), which stay valid if the question is ever reopened.

#include "driver/Uc8253X3Driver.h"

namespace freeink {

const Uc8253X3Config& lectorUc8253X3Config() { return uc8253X3DefaultConfig(); }

}  // namespace freeink
