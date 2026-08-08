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

// Called at the end of every batch to make the written lines durable. Separate from
// LineSink because the cost belongs once per batch, not once per line: without it the
// records sit in the storage layer's buffer and a deep sleep (a chip reset) throws them
// away, which is exactly how the first measurement run lost 13 of its 14 files.
using CommitSink = void (*)();

// Installs the sinks and writes the CSV header through them.
void begin(LineSink sink, CommitSink commit);

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

// Which round of the sweep this boot is running, 1-based, 0 when unset. Set once at boot
// by whoever picks the candidate (src/PerfLogSink.cpp), read by GfxRenderer so every
// refresh can stamp the number on screen. It lives here rather than in the sink because
// this is the one header both the sink and the renderer already share.
//
// The reason it exists at all: the sweep asks the reader which round looked and felt best,
// and a round is a whole boot. Counting boots in your head is exactly the kind of thing
// that goes wrong quietly, and lector.exp.11 was already lost once to a bookkeeping bug.
void setRound(uint8_t round);
uint8_t round();

// Total rounds in the sweep, so the on-screen marker can read "3 of 4" rather than "3".
void setRoundCount(uint8_t count);
uint8_t roundCount();

#else

using LineSink = bool (*)(const char* line);
using CommitSink = void (*)();
inline void begin(LineSink, CommitSink) {}
inline void setScreen(const char*) {}
inline void record(uint8_t, uint8_t, uint32_t, uint32_t) {}
inline void flush() {}
inline void setRound(uint8_t) {}
inline uint8_t round() { return 0; }
inline void setRoundCount(uint8_t) {}
inline uint8_t roundCount() { return 0; }

#endif

}  // namespace PerfLog
