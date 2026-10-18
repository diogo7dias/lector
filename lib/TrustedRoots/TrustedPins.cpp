#include "TrustedPins.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>

namespace trusted_pins {

namespace {
// MAX_PINS lines of at most a 255-byte host, a space, 64 hex and a newline.
constexpr size_t MAX_FILE_BYTES = MAX_PINS * 322;

Rejection pending;
bool hasPending = false;

// The pin file, or empty. Heap: up to ~5 KB, read only around a TLS connect.
std::string readPins() {
  auto buf = makeUniqueNoThrow<char[]>(MAX_FILE_BYTES);
  if (!buf) {
    LOG_ERR("PINS", "OOM: %u bytes", static_cast<unsigned>(MAX_FILE_BYTES));
    return {};
  }
  const size_t len = Storage.readFileToBuffer(FILE_PATH, buf.get(), MAX_FILE_BYTES);
  return std::string(buf.get(), len);
}
}  // namespace

bool find(const std::string_view host, uint8_t out[SHA256_SIZE]) {
  if (host.empty() || !Storage.exists(FILE_PATH)) return false;
  return findPin(readPins(), host, out);
}

bool save(const std::string_view host, const uint8_t sha[SHA256_SIZE]) {
  const std::string text = withPin(Storage.exists(FILE_PATH) ? readPins() : std::string{}, host, sha);
  // Under .part and renamed when whole: a reset mid-write must not cost every other pin.
  const std::string part = FsHelpers::partialPathFor(FILE_PATH);
  Storage.ensureDirectoryExists("/.crosspoint");
  {
    HalFile file;
    if (!Storage.openFileForWrite("PINS", part.c_str(), file) ||
        file.write(reinterpret_cast<const uint8_t*>(text.data()), text.size()) != text.size()) {
      LOG_ERR("PINS", "Could not write %s", part.c_str());
      file.close();
      Storage.remove(part.c_str());
      return false;
    }
  }
  if ((Storage.exists(FILE_PATH) && !Storage.remove(FILE_PATH)) || !Storage.rename(part.c_str(), FILE_PATH)) {
    LOG_ERR("PINS", "Could not replace %s", FILE_PATH);
    return false;
  }
  LOG_INF("PINS", "Trusted the certificate of %.*s", static_cast<int>(host.size()), host.data());
  return true;
}

void noteRejected(const std::string_view host, const uint8_t sha[SHA256_SIZE]) {
  pending.host.assign(host.data(), host.size());
  std::memcpy(pending.sha, sha, SHA256_SIZE);
  hasPending = true;
}

bool takeRejected(Rejection& out) {
  if (!hasPending) return false;
  out = pending;
  hasPending = false;
  return true;
}

}  // namespace trusted_pins
