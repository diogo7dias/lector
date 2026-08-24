#pragma once

#include <cstddef>

// Stage stamps for the lock path, the sleep-side counterpart to WakeTiming.
//
// WakeTiming has a fixed stage list because the boot always runs the same steps in the
// same order. The lock does not: which sleep face renders decides what work happens, so
// the marks are free-form and only the ones a given lock actually reached are reported.
//
// The measurement that prompted this: a lock cost 10258 ms, of which the panel accounted
// for about 4800 ms. The remaining 5300 ms was inside the sleep screen build with nothing
// to say where it went.
namespace SleepTiming {

// Marks are dropped from the moment enterDeepSleep() starts, so every stamp is relative
// to the button press rather than to the previous mark.
void begin();

// `name` must be a string literal or otherwise outlive the sleep: only the pointer is
// kept. Silently ignored once the (small, fixed) table is full, so an unexpectedly chatty
// path cannot allocate or overrun on the way into sleep.
void mark(const char* name);

// "favs=12 popup=210 decode=3980 face=8600 state=15", oldest first, each the milliseconds
// since begin(). Empty string when nothing was marked.
void format(char* out, size_t outLen);

}  // namespace SleepTiming
