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
// Always compiled, off unless the "Performance timings" setting is on — the same switch
// PerfLogSink answers to. The busiest call site fires once per touch contact, so leaving
// it on by default would be a card write per tap forever. With the setting off, begin()
// returns before opening anything and every note() call returns on its first line.
namespace debug_trace {

// Opens this boot's file. Call once, after Storage.begin() has succeeded.
void begin();

// Appends one line, printf-style, prefixed with millis(). No-op when the file could not
// be opened.
void note(const char* format, ...) __attribute__((format(printf, 1, 2)));

}  // namespace debug_trace
