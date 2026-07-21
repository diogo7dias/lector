#include "StringUtils.h"

#include <Utf8.h>

#ifdef USE_RUST_FSHELPERS
#include <cstdint>

#include "fshelpers_rs.h"
#endif

namespace StringUtils {

std::string sanitizeFilename(const std::string& name, size_t maxBytes) {
#ifdef USE_RUST_FSHELPERS
  // Memory-safe Rust implementation (rust/fshelpers-rs): bounds-checked UTF-8
  // decode, no reads past the input on truncated multibyte sequences. Rust writes
  // into a caller buffer; the result never exceeds max(maxBytes, 4) ("book").
  const size_t cap = maxBytes < 4 ? 4 : maxBytes;
  std::string out(cap, '\0');
  size_t n = fshelpers_sanitize_filename(reinterpret_cast<const uint8_t*>(name.data()), name.size(), maxBytes,
                                         reinterpret_cast<uint8_t*>(&out[0]), out.size());
  if (n > out.size()) n = out.size();
  out.resize(n);
  return out;
#else
  std::string result;
  result.reserve(std::min(name.size(), maxBytes));

  const auto* text = reinterpret_cast<const unsigned char*>(name.c_str());

  // Skip leading spaces and dots so they don't consume the byte budget
  while (*text == ' ' || *text == '.') {
    text++;
  }

  // Process full UTF-8 codepoints to avoid trimming in the middle of a multibyte sequence
  while (*text != 0) {
    const auto* cpStart = text;
    uint32_t cp = utf8NextCodepoint(&text);

    if (cp == '/' || cp == '\\' || cp == ':' || cp == '*' || cp == '?' || cp == '"' || cp == '<' || cp == '>' ||
        cp == '|') {
      // Replace illegal and control characters with '_'
      if (result.length() + 1 > maxBytes) break;
      result += '_';
    } else if (cp >= 128 || (cp >= 32 && cp < 127)) {
      const size_t cpBytes = text - cpStart;
      if (result.length() + cpBytes > maxBytes) break;
      result.append(reinterpret_cast<const char*>(cpStart), cpBytes);
    }
  }

  // Trim trailing spaces and dots
  size_t end = result.find_last_not_of(" .");
  if (end != std::string::npos) {
    result.resize(end + 1);
  } else {
    result.clear();
  }

  return result.empty() ? "book" : result;
#endif
}

}  // namespace StringUtils
