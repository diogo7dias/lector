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

// --- wolfSSL heap allocation tracer (diagnostic, opt-in) ----------------------
// A TLS handshake fails (-2 / MP_MEM) once a reading session fragments the heap.
// The fresh-boot heap handshakes fine; a fragmented one does not. To fix the
// right allocation we must first SEE which one dies and how big it is, instead
// of guessing. When FREEINK_WOLFSSL_MEM_TRACE is set we route wolfSSL through
// custom allocators that log every large request and any failure, with the
// largest free block at the moment of failure. Raw Serial (not LOG_*) so it
// prints regardless of LOG_LEVEL in a release diagnostic build. Remove the flag
// once the failing allocation is identified.
#if defined(FREEINK_NET_WOLFSSL) && defined(FREEINK_WOLFSSL_MEM_TRACE)
#include <Esp.h>  // ESP.getFreeHeap / getMaxAllocHeap

#include <cstdlib>

#include <wolfssl/wolfcrypt/memory.h>

// NOTE: <Logging.h> above #defines Serial -> MySerialImpl::instance, so raw
// Serial.printf does not link in src/. Route the trace through LOG_ERR/LOG_INF
// (LOG_ERR always prints; LOG_INF prints at LOG_LEVEL>=1, i.e. gh_release too).
namespace {
constexpr size_t kTraceThreshold = 2048;  // catch fastmath fp_int temporaries (~2KB) and larger

extern "C" void* freeinkWolfMalloc(size_t size) {
  void* p = malloc(size);
  if (!p) {
    LOG_ERR("WOLFMEM", "MALLOC FAIL %u (free=%u maxblk=%u)", (unsigned)size, (unsigned)ESP.getFreeHeap(),
            (unsigned)ESP.getMaxAllocHeap());
  } else if (size >= kTraceThreshold) {
    LOG_INF("WOLFMEM", "malloc %u (maxblk=%u)", (unsigned)size, (unsigned)ESP.getMaxAllocHeap());
  }
  return p;
}

extern "C" void freeinkWolfFree(void* ptr) { free(ptr); }

extern "C" void* freeinkWolfRealloc(void* ptr, size_t size) {
  void* p = realloc(ptr, size);
  if (!p && size) {
    LOG_ERR("WOLFMEM", "REALLOC FAIL %u (free=%u maxblk=%u)", (unsigned)size, (unsigned)ESP.getFreeHeap(),
            (unsigned)ESP.getMaxAllocHeap());
  } else if (size >= kTraceThreshold) {
    LOG_INF("WOLFMEM", "realloc %u (maxblk=%u)", (unsigned)size, (unsigned)ESP.getMaxAllocHeap());
  }
  return p;
}
}  // namespace

extern "C" void freeinkWolfMemTraceInit() {
  static bool installed = false;
  if (installed) return;
  installed = true;
  wolfSSL_SetAllocators(freeinkWolfMalloc, freeinkWolfFree, freeinkWolfRealloc);
}
#endif
