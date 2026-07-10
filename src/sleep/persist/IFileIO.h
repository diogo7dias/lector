/**
 * @file IFileIO.h
 * @brief Abstraction over filesystem ops used by PersistentStore<T>.
 *
 * Production: SdFatFileIO wraps HalStorage (SD card). Host tests:
 * InMemoryFileIO uses a std::unordered_map<path, content>. Keeps
 * PersistentStore and PersistManager hardware-free and unit-testable
 * without an ESP32 toolchain.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace crosspoint {
namespace persist {

// Minimal byte sink for streaming serialization. ArduinoJson accepts any
// object with write(uint8_t) + write(uint8_t*,size_t) — this abstracts
// over "HalFile on device" vs "std::string append on host tests" so the
// serializer never builds a full intermediate string (avoids OOM on large
// APP_STATE, see PR #41 / issue #21 postmortem).
struct JsonSink {
  virtual ~JsonSink() = default;
  virtual size_t write(uint8_t b) = 0;
  virtual size_t write(const uint8_t* buf, size_t n) = 0;
};

// Producer: called with an open sink; fills it with bytes. Returns true
// iff every write succeeded. Callee typically does `serializeJson(doc, sink)`.
using StreamProducer = std::function<bool(JsonSink&)>;

struct IFileIO {
  virtual ~IFileIO() = default;

  // Atomic write: content → `path`, rotating any prior file to `.bak`
  // and using `.tmp` as the intermediate. Creates parent directory if
  // missing. Returns true on success.
  virtual bool safeWrite(const std::string& path, const std::string& content) = 0;

  // Streaming atomic write: opens the tmp file, hands a JsonSink to
  // `produce`, then performs the same .tmp → .bak → real rotation as
  // safeWrite. The serializer writes directly to the file (no std::string
  // intermediate) so peak heap stays bounded regardless of payload size.
  virtual bool safeWriteStreamed(const std::string& path, const StreamProducer& produce) = 0;

  // Read with crash-safe fallback: real → `.bak` → `.tmp`. Returns
  // empty string if none of the three yielded content.
  virtual std::string safeRead(const std::string& path) = 0;

  // Byte size of the file safeRead() would read (real → `.bak` → `.tmp`), or 0
  // if none exist. Lets a caller heap-probe BEFORE reading so an -fno-exceptions
  // bad_alloc inside the read can't abort. Default reads the whole file (host
  // tests are not heap-constrained); the SD impl overrides with a cheap stat.
  virtual size_t readableSize(const std::string& path) { return safeRead(path).size(); }

  // Streaming read into `out` (real → `.bak` → `.tmp`), filling it in fixed
  // chunks with a single reserve — peak heap is 1× the file, not the ~2-3× that
  // safeRead's Arduino String + std::string copy costs. The CALLER must heap-gate
  // via readableSize() first (this method reserves `out` and would bad_alloc if
  // the heap can't afford it). Returns true iff bytes were read. Default forwards
  // to safeRead so host fakes need no override.
  virtual bool readStreamed(const std::string& path, std::string& out) {
    out = safeRead(path);
    return !out.empty();
  }

  // Existence / management used for sidecar backup + diagnostics.
  virtual bool exists(const std::string& path) = 0;
  virtual bool mkdir(const std::string& path) = 0;
  virtual bool copy(const std::string& from, const std::string& to) = 0;
};

}  // namespace persist
}  // namespace crosspoint
