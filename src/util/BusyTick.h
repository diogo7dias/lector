#pragma once

// Heartbeat for long-running work.
//
// Slow operations here are blocking scans and parses on the main task: a folder
// listing, a font load, a cache build. They already pause periodically to feed
// the watchdog, and that same point is where they should let the UI say
// "something is happening".
//
// This is a bare function-pointer hook rather than a direct call into the
// renderer so that scanning code in util/ and lib/ keeps no dependency on the
// display stack, and so the cost is exactly zero when nothing is listening.
// BusyBanner installs itself as the handler for the length of one operation.
namespace busy {

// Called from inside a long loop, next to the watchdog feed. No-op unless a
// handler is installed. The listener decides whether enough time has passed to
// be worth telling the user about.
void tick();

// Called immediately before a step that is slow every single time it runs (a
// multi-megabyte font parse, a cache build). Tells the listener not to wait out
// its usual delay. No-op unless a handler is installed.
void tickNow();

// Installs (or clears, with nullptr) the handlers. Not reentrant: one slow
// operation is in flight at a time, and BusyBanner's guard enforces it.
void setTickHandler(void (*handler)(), void (*nowHandler)());

}  // namespace busy
