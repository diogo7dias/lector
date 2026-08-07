#pragma once

// Installs PerfLog's card sink and opens this session's CSV. No-op unless PERF_LOG is
// set. Call once, after Storage.begin() has succeeded.
//
// `device` tags the file name, "x3" or "x4", so the two devices' runs never land in the
// same file. See docs/perf-measurement.md.
void startPerfLogSink(const char* device);
