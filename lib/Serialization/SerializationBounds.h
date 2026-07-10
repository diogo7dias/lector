#pragma once

#include <cstddef>
#include <cstdint>

namespace serialization {

inline constexpr size_t DEFAULT_MAX_STRING_BYTES = 4096;

inline bool isStringLengthValid(const uint32_t length, const size_t remainingBytes,
                                const size_t maxBytes = DEFAULT_MAX_STRING_BYTES) {
  return static_cast<size_t>(length) <= maxBytes && static_cast<size_t>(length) <= remainingBytes;
}

inline constexpr bool isArrayCountValid(const size_t count, const size_t remaining, const size_t elementSize,
                                        const size_t maxCount) {
  return elementSize != 0 && count <= maxCount && count <= remaining / elementSize;
}

inline bool hasAllocationHeadroom(const size_t length, const size_t largestBlock, const size_t headroom) {
  return length == 0 || (length <= largestBlock && largestBlock - length >= headroom);
}

}  // namespace serialization
