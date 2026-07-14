#pragma once
// Host-test stub: firmware logging macros become no-ops.
#define LOG_DBG(tag, fmt, ...) ((void)0)
#define LOG_INF(tag, fmt, ...) ((void)0)
#define LOG_ERR(tag, fmt, ...) ((void)0)
