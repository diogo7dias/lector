#pragma once

// Host-test stub for the firmware Logging.h. The real header pulls in
// <HardwareSerial.h>, which does not exist off-device. Arena.h only needs the
// LOG_* macros to expand to nothing so the pure allocator logic can be tested
// on the host. Kept intentionally minimal.

#define LOG_ERR(origin, format, ...) ((void)0)
#define LOG_INF(origin, format, ...) ((void)0)
#define LOG_DBG(origin, format, ...) ((void)0)
