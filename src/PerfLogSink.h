#pragma once

// Installs PerfLog's card sink and opens this session's CSV.
//
// Compiled into every build; whether it does anything is decided at runtime by the
// "Performance timings" setting. With the setting off it returns before touching the
// card, PerfLog::begin() is never called, and every refresh's record() call returns on
// its first line.
//
// Call once, after Storage.begin() has succeeded and after settings have loaded.
//
// `device` tags the file name, "x3" or "x4", so the two devices' runs never land in the
// same file. See docs/perf-measurement.md.
void startPerfLogSink(const char* device);

// Appends the previous wake's stage breakdown to the open CSV, as a comment line. Called
// once the wake stamps have been read back off the card, so a single copied file answers
// page-turn cost and wake cost together instead of two files needing to be lined up by
// hand. No-op when the sink is not running.
void logWakeTimingToPerfLog();

// Appends the per-mode refresh totals for this session. Called on the way into sleep,
// after the last refresh, so the file ends with the summary of what preceded it.
void logPerfSummary();
