#pragma once
// Silent unless FUZZ_TRACE is set, so a corpus replay can show what the firmware would log.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace logging_stub {
inline void sink(const char* origin, const char* format, ...) {
  static const bool on = getenv("FUZZ_TRACE") != nullptr;
  if (!on) return;
  va_list args;
  va_start(args, format);
  fprintf(stderr, "[%s] ", origin);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
}
}  // namespace logging_stub

#define LOG_ERR(origin, format, ...) logging_stub::sink(origin, format __VA_OPT__(, ) __VA_ARGS__)
#define LOG_INF(origin, format, ...) logging_stub::sink(origin, format __VA_OPT__(, ) __VA_ARGS__)
#define LOG_DBG(origin, format, ...) logging_stub::sink(origin, format __VA_OPT__(, ) __VA_ARGS__)
