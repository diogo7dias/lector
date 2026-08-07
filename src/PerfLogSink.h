#pragma once

// Installs PerfLog's card sink and opens this session's CSV. No-op unless PERF_LOG is
// set. Call once, after Storage.begin() has succeeded.
//
// `device` tags the file name, "x3" or "x4", so the two devices' runs never land in the
// same file. See docs/perf-measurement.md.
void startPerfLogSink(const char* device);

// The PLL byte the X3 panel should be initialised with, i.e. its frame clock. Returns the
// driver's stock 0x09 unless PERF_LOG is set, in which case it walks a short candidate
// list, one value per boot, so a single flash measures the whole sweep. See
// src/platform/LectorUc8253X3Config.cpp for why the value is swept rather than chosen.
//
// Called from panel init, which runs before the sink is started, so this reads the card
// itself rather than depending on any of the logging state.
unsigned char lectorX3PllByte();
