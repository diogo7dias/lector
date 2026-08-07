#pragma once

#include <cstdint>

// Panel refresh instrumentation, for the measurement runs in docs/perf-measurement.md.
//
// Every function here compiles to an empty inline body unless PERF_LOG is defined, so a
// stable build carries no records, no buffer and no SD write. It is a throwaway build
// flag, exactly like WAKE_TIMING_OVERLAY was: switch it on, take the numbers, switch it
// off again.
//
// What it records, and what it deliberately does not:
//
// Each refresh yields one CSV line with the mode asked for, the mode the driver actually
// ran, and the time the call took. The requested and actual modes are separate columns
// because both panel drivers override the request in places, and the size of that gap is
// itself one of the things being measured.
//
// The bus / waveform / post-work split can only be seen from inside the SDK, which is a
// submodule this build does not fork for a measurement. What it can see instead is the
// async split: displayBufferAsync() returns once the waveform has been fired, and
// waitRefreshComplete() covers the wait plus the driver's post-work. On paths that use
// it, those two numbers separate "pushing bytes" from "waiting for ink and cleaning up",
// which is the distinction the speed work actually turns on.
//
// Records are held in a fixed static buffer and written in batches. Writing to the card
// on every refresh would add card latency to the very number being measured.
namespace PerfLog {

#if defined(PERF_LOG) && PERF_LOG

// Where formatted lines go. Installed by the owner of the storage layer (see
// src/PerfLogSink.cpp) so this library stays a pure recorder: it buffers and formats,
// and knows nothing about SD cards. Returns false when the line could not be written,
// which is counted and reported in the log itself rather than retried.
using LineSink = bool (*)(const char* line);

// Installs the sink and writes the CSV header through it.
void begin(LineSink sink);

// Names the screen that the following refreshes belong to. The name is copied, not
// referenced: activities are heap-allocated and deleted on exit, so a stored pointer
// would dangle for any refresh taken between one activity's destruction and the next
// one's onEnter().
void setScreen(const char* screenName);

// One completed refresh. Times are microseconds. asyncStartUs is the part that returned
// before the panel finished (0 on a blocking refresh).
void record(uint8_t requestedMode, uint8_t actualMode, uint32_t totalUs, uint32_t asyncStartUs);

// Writes whatever is buffered. Called automatically when the buffer fills; call it
// directly before sleeping so a run is never lost to a lock.
void flush();

#else

using LineSink = bool (*)(const char* line);
inline void begin(LineSink) {}
inline void setScreen(const char*) {}
inline void record(uint8_t, uint8_t, uint32_t, uint32_t) {}
inline void flush() {}

#endif

}  // namespace PerfLog
