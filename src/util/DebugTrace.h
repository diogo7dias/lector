#pragma once

// A trace file on the card, for the questions serial cannot answer.
//
// The X4 Pro speaks over USB-Serial/JTAG: the port is created by the firmware itself, so
// it disappears on every reset and sleep, and a test-kit session routinely records the
// boot and then nothing at all. Anything that has to be observed while the device is in
// someone's hands — a gesture that does not fire, a panel that does not appear — cannot
// be chased through that channel.
//
// So these lines also go to /trace-<n>.log on the card. One file per boot, because deep
// sleep is a chip reset and SDCardManager has no append mode: reopening the same path
// would truncate everything recorded before it (the same reason PerfLogSink numbers its
// files). Flushed per line, so a device that sleeps mid-session still leaves the trace
// behind.
//
// Always compiled, always on: the calls are rare (a gesture, a bound action, a panel
// opening) and a card write of a few dozen bytes costs about a millisecond. Nothing here
// runs per frame or per pixel.
namespace debug_trace {

// Opens this boot's file. Call once, after Storage.begin() has succeeded.
void begin();

// Appends one line, printf-style, prefixed with millis(). No-op when the file could not
// be opened.
void note(const char* format, ...) __attribute__((format(printf, 1, 2)));

}  // namespace debug_trace
