// wolfSSL (Arduino-wolfSSL logging.c) references wolfSSL_Arduino_Serial_Print for
// its debug logging when WOLFSSL logging is compiled in. Define it once here so
// any SecureNet consumer (KOSync, and later OPDS/OTA) links, independent of which
// call sites have been ported. Routed to the normal firmware log. Upstream #2475
// defined this inline in HttpDownloader; kept standalone here so it exists even
// before HttpDownloader is ported.
#if defined(FREEINK_NET_WOLFSSL)
#include <Logging.h>

extern "C" void wolfSSL_Arduino_Serial_Print(const char* const msg) { LOG_DBG("WOLFSSL", "%s", msg); }
#endif
