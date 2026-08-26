#include "DebugTrace.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <PerfLog.h>

#include <cstdarg>
#include <cstdio>

namespace {

// Held open for the whole session: reopening would truncate, and an open/close pair per
// line would cost far more than the write itself.
HalFile traceFile;

// Enough for the longest line any call site writes, with room to spare. Truncation is
// silent and harmless: a clipped trace line is still a trace line.
constexpr size_t kLineMax = 160;
constexpr int kMaxSessions = 50;

}  // namespace

namespace debug_trace {

void begin() {
  if (traceFile.isOpen()) return;
  if (!Storage.ready()) return;
  char path[24];
  for (int session = 1; session <= kMaxSessions; ++session) {
    snprintf(path, sizeof(path), "/trace-%d.log", session);
    if (Storage.exists(path)) continue;
    if (Storage.openFileForWrite("TRACE", path, traceFile)) {
      note("trace opened");
    }
    return;
  }
  // Every slot taken: the oldest is the least interesting, so the newest run wins it.
  snprintf(path, sizeof(path), "/trace-%d.log", kMaxSessions);
  if (Storage.openFileForWrite("TRACE", path, traceFile)) note("trace opened (reused)");
}

void note(const char* format, ...) {
  if (!traceFile.isOpen()) return;
  char line[kLineMax];
  const int prefix = snprintf(line, sizeof(line), "[%lu] ", millis());
  if (prefix < 0 || static_cast<size_t>(prefix) >= sizeof(line)) return;
  va_list args;
  va_start(args, format);
  vsnprintf(line + prefix, sizeof(line) - prefix, format, args);
  va_end(args);
  traceFile.print(line);
  traceFile.print("\n");
  traceFile.flush();
}

}  // namespace debug_trace
