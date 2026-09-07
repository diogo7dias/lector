#pragma once
// Host tests have no serial; the parser only logs.
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_DBG(...) ((void)0)
