#pragma once

#include <cstdint>

// Panel refresh instrumentation, for the measurement runs in docs/perf-measurement.md.
//
// This used to be a compile-time build flag (PERF_LOG). It is now compiled into every
// build and switched on at runtime by the "Performance timings" setting, because the
// numbers are the hand-off channel: the device writes a CSV to the card, the card is
// read on a computer, and no measurement run needs a special firmware to exist. With the
// setting off, begin() is never called, the sinks stay null, and record() returns on its
// first line — no buffer is touched and nothing reaches the card.
//
// What it records, and what it deliberately does not:
//
// Each refresh yields one CSV line with the mode asked for, the mode the driver actually
// ran, the time the call took, and the think time before it (see PerfStats). The
// requested and actual modes are separate columns because both panel drivers override
// the request in places, and the size of that gap is itself one of the things being
// measured.
//
// The bus / waveform / post-work split can only be seen from inside the SDK. What this
// can see instead is the async split: displayBufferAsync() returns once the waveform has
// been fired, and waitRefreshComplete() covers the wait plus the driver's post-work. On
// paths that use it, those two numbers separate "pushing bytes" from "waiting for ink and
// cleaning up", which is the distinction the speed work actually turns on.
//
// Records are held in a fixed static buffer and written in batches. Writing to the card
// on every refresh would add card latency to the very number being measured.
namespace PerfLog {

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

// Installs the sinks and writes the CSV column header through them. Until this is
// called, every other function here is a no-op.
void begin(LineSink sink, CommitSink commit);

// True once begin() has installed a working sink. Callers that would do work only to
// feed the log (formatting a screen name, say) can skip it.
bool isActive();

// Names the screen that the following refreshes belong to. The name is copied, not
// referenced: activities are heap-allocated and deleted on exit, so a stored pointer
// would dangle for any refresh taken between one activity's destruction and the next
// one's onEnter().
void setScreen(const char* screenName);

// One completed refresh. Times are microseconds. asyncStartUs is the part that returned
// before the panel finished (0 on a blocking refresh). thinkMs is the gap from the input
// that caused this paint, or PerfStats::kNoThink when no press was outstanding.
// wireUs and waveUs split the total into bus time and waveform time; see
// PerfStats::noteRefresh for why the split is the number that decides what to change.
void record(uint8_t requestedMode, uint8_t actualMode, uint32_t totalUs, uint32_t asyncStartUs, uint16_t thinkMs,
            uint16_t inkScore, uint16_t inkDebt, uint32_t wireUs, uint32_t waveUs, bool turbo, uint8_t diag,
            uint16_t settleMs);

// A free-form annotation line, written verbatim as a CSV comment. Used for the wake-stage
// breakdown and the end-of-session summary, so one copied file answers page turn, menu
// and wake together instead of three files needing to be lined up by hand.
void note(const char* text);

// Writes whatever is buffered. Called automatically when the buffer fills; call it
// directly before sleeping so a run is never lost to a lock.
void flush();

}  // namespace PerfLog
