#pragma once

#include <strings.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "TrustedRoots.h"

// Certificates the user chose to trust, by host: the SHA-256 of a server's exact leaf
// certificate, for a server whose chain the bundled roots cannot vouch for (a
// self-signed home server, a private CA). freeink::cert_pin checks it at the handshake.
//
// The file is text, one "host hex-sha256" per line, so it can be read and edited on a
// computer. The parsing below is pure for the host test; TrustedPins.cpp does the I/O.
namespace trusted_pins {

constexpr size_t SHA256_SIZE = 32;
constexpr size_t MAX_PINS = 16;
inline constexpr char FILE_PATH[] = "/.crosspoint/trusted_certs.txt";

namespace detail {
inline int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Calls fn(host, hex) for each well-formed line; malformed lines are skipped.
template <class Fn>
void forEachLine(std::string_view text, Fn fn) {
  while (!text.empty()) {
    const size_t eol = text.find('\n');
    std::string_view line = text.substr(0, eol);
    text = eol == std::string_view::npos ? std::string_view{} : text.substr(eol + 1);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    const size_t space = line.find(' ');
    if (space == 0 || space == std::string_view::npos) continue;
    const std::string_view hex = line.substr(space + 1);
    if (hex.size() != SHA256_SIZE * 2) continue;
    fn(line.substr(0, space), hex);
  }
}

inline bool sameHost(std::string_view a, std::string_view b) {
  return a.size() == b.size() && strncasecmp(a.data(), b.data(), a.size()) == 0;
}
}  // namespace detail

// The pin `text` holds for `host`, into `out`.
inline bool findPin(std::string_view text, std::string_view host, uint8_t out[SHA256_SIZE]) {
  bool found = false;
  detail::forEachLine(text, [&](std::string_view h, std::string_view hex) {
    if (found || !detail::sameHost(h, host)) return;
    for (size_t i = 0; i < SHA256_SIZE; ++i) {
      const int hi = detail::hexValue(hex[2 * i]);
      const int lo = detail::hexValue(hex[2 * i + 1]);
      if (hi < 0 || lo < 0) return;
      out[i] = static_cast<uint8_t>(hi << 4 | lo);
    }
    found = true;
  });
  return found;
}

// `text` with `host` pinned to `sha` (replacing its old pin), newest last. Past MAX_PINS
// the oldest go.
inline std::string withPin(std::string_view text, std::string_view host, const uint8_t sha[SHA256_SIZE]) {
  std::vector<std::string_view> kept;
  kept.reserve(MAX_PINS);
  detail::forEachLine(text, [&](std::string_view h, std::string_view hex) {
    if (detail::sameHost(h, host)) return;
    if (kept.size() == MAX_PINS - 1) kept.erase(kept.begin());              // room for the new one
    kept.push_back(std::string_view(h.data(), h.size() + 1 + hex.size()));  // the whole line
  });
  std::string out;
  for (const auto line : kept) {
    out += line;
    out += '\n';
  }
  out += host;
  out += ' ';
  char hex[3];
  for (size_t i = 0; i < SHA256_SIZE; ++i) {
    snprintf(hex, sizeof(hex), "%02x", sha[i]);
    out += hex;
  }
  out += '\n';
  return out;
}

// The first 12 bytes in groups of four hex digits, "C041 C07E 05F4 ...": enough to
// compare against what the server shows, short enough for one line on the panel.
inline void formatFingerprint(const uint8_t sha[SHA256_SIZE], char* out, size_t outSize) {
  size_t pos = 0;
  for (size_t i = 0; i < 12 && pos + 5 < outSize; i += 2) {
    pos += snprintf(out + pos, outSize - pos, i ? " %02X%02X" : "%02X%02X", sha[i], sha[i + 1]);
  }
  if (outSize) out[pos < outSize ? pos : outSize - 1] = '\0';
}

// --- Device side (TrustedPins.cpp) ----------------------------------------------

// The pin saved for `host`.
bool find(std::string_view host, uint8_t out[SHA256_SIZE]);
// Pin `host` to `sha` on the card.
bool save(std::string_view host, const uint8_t sha[SHA256_SIZE]);

// The last handshake that reached a server and did not trust it. One slot: the
// request that failed records it, the screen that asks the user takes it.
struct Rejection {
  std::string host;
  uint8_t sha[SHA256_SIZE] = {};
};
void noteRejected(std::string_view host, const uint8_t sha[SHA256_SIZE]);
bool takeRejected(Rejection& out);

// trusted_roots::apply with the card's pin for `url`'s host.
template <class HttpClient>
void applyWithPin(HttpClient& http, const char* url) {
  uint8_t pin[SHA256_SIZE];
  trusted_roots::apply(http, url, find(trusted_roots::httpsHost(url), pin) ? pin : nullptr);
}

// After a request that failed before any response: true, and the rejection noted for
// the screen to ask about, when the TLS peer was reached but not trusted.
template <class HttpClient>
bool noteIfRejected(const HttpClient& http, const char* url) {
  if (!http.certRejected()) return false;
  noteRejected(trusted_roots::httpsHost(url), http.rejectedCert());
  return true;
}

}  // namespace trusted_pins
