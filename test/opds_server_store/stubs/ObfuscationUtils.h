#pragma once

// Host stand-in for lib/Serialization/ObfuscationUtils.h. The firmware XORs with the chip's
// MAC and base64-encodes; this XORs with a fixed byte and hex-encodes. Same contract: the
// plaintext never appears in the output, and damaged input reports ok = false.
#include <cstddef>
#include <string>

namespace obfuscation {

inline std::string obfuscateToBase64(const std::string& plaintext) {
  static const char digits[] = "0123456789abcdef";
  std::string out;
  for (const char c : plaintext) {
    const auto byte = static_cast<unsigned char>(c ^ 0x5A);
    out += digits[byte >> 4];
    out += digits[byte & 15];
  }
  return out;
}

inline std::string deobfuscateFromBase64(const char* encoded, const size_t maxDecodedLength, bool* ok, bool* tooLong) {
  if (tooLong) *tooLong = false;
  if (ok) *ok = false;
  const std::string text = encoded ? encoded : "";
  if (text.empty() || text.size() % 2 != 0 || text.size() / 2 > maxDecodedLength) return "";
  std::string out;
  for (size_t i = 0; i < text.size(); i += 2) {
    const auto nibble = [](const char c) {
      return c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= '0' && c <= '9' ? c - '0' : -1);
    };
    const int hi = nibble(text[i]);
    const int lo = nibble(text[i + 1]);
    if (hi < 0 || lo < 0) return "";
    out += static_cast<char>((hi * 16 + lo) ^ 0x5A);
  }
  if (ok) *ok = true;
  return out;
}

}  // namespace obfuscation
