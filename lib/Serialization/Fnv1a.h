#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

// FNV-1a (Fowler/Noll/Vo), byte at a time: xor the byte, then multiply by the prime.
// These values are persisted (cache file names, cache keys, font ids), so the constants
// and the mixing order must never change. `hash` continues an earlier result, which is how
// callers fold several fields into one key.
namespace fnv1a {

constexpr uint32_t OFFSET_BASIS_32 = 2166136261u;
constexpr uint32_t PRIME_32 = 16777619u;
constexpr uint64_t OFFSET_BASIS_64 = 14695981039346656037ull;
constexpr uint64_t PRIME_64 = 1099511628211ull;

constexpr uint32_t mix32(const uint32_t hash, const uint8_t byte) { return (hash ^ byte) * PRIME_32; }

// Takes uint8_t, not void: a const char* argument must reach the string_view overload.
inline uint32_t hash32(const uint8_t* data, const size_t len, uint32_t hash = OFFSET_BASIS_32) {
  for (size_t i = 0; i < len; i++) hash = mix32(hash, data[i]);
  return hash;
}

inline uint32_t hash32(const std::string_view text, const uint32_t hash = OFFSET_BASIS_32) {
  return hash32(reinterpret_cast<const uint8_t*>(text.data()), text.size(), hash);
}

inline uint64_t hash64(const std::string_view text, uint64_t hash = OFFSET_BASIS_64) {
  for (const char c : text) hash = (hash ^ static_cast<uint8_t>(c)) * PRIME_64;
  return hash;
}

}  // namespace fnv1a
