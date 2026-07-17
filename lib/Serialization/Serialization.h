#pragma once
#include <HalStorage.h>

#include <iostream>

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#endif

#include "SerializationBounds.h"

namespace serialization {
template <typename T>
void writePod(std::ostream& os, const T& value) {
  os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
void writePod(HalFile& file, const T& value) {
  file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

template <typename T>
void readPod(std::istream& is, T& value) {
  is.read(reinterpret_cast<char*>(&value), sizeof(T));
}

template <typename T>
void readPod(HalFile& file, T& value) {
  file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T));
}

// Checked variant: false on a short read (truncated/corrupt file) so callers
// can bail instead of consuming garbage.
template <typename T>
bool tryReadPod(HalFile& file, T& value) {
  return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T)) == static_cast<int>(sizeof(T));
}

// Checked variant: false on a short write (SD full / IO error) so cache
// builders can abort instead of committing a truncated file.
template <typename T>
bool tryWritePod(HalFile& file, const T& value) {
  return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T)) == sizeof(T);
}

inline void writeString(std::ostream& os, const std::string& s) {
  const uint32_t len = s.size();
  writePod(os, len);
  os.write(s.data(), len);
}

inline void writeString(HalFile& file, const std::string& s) {
  const uint32_t len = s.size();
  writePod(file, len);
  file.write(reinterpret_cast<const uint8_t*>(s.data()), len);
}

// Checked variant of writeString; false on any short write.
inline bool tryWriteString(HalFile& file, const std::string& s) {
  const uint32_t len = s.size();
  if (!tryWritePod(file, len)) return false;
  return len == 0 || file.write(reinterpret_cast<const uint8_t*>(s.data()), len) == len;
}

inline bool hasStringAllocationHeadroom(const size_t length) {
#if defined(ARDUINO_ARCH_ESP32)
  constexpr size_t ALLOCATION_HEADROOM = 2048;
  const size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
  return hasAllocationHeadroom(length, largestBlock, ALLOCATION_HEADROOM);
#else
  (void)length;
  return true;
#endif
}

inline bool readString(std::istream& is, std::string& s, const size_t maxBytes = DEFAULT_MAX_STRING_BYTES) {
  uint32_t len = 0;
  if (!is.read(reinterpret_cast<char*>(&len), sizeof(len))) {
    s.clear();
    return false;
  }

  const auto dataStart = is.tellg();
  if (dataStart == std::istream::pos_type(-1)) {
    s.clear();
    is.setstate(std::ios::failbit);
    return false;
  }
  is.seekg(0, std::ios::end);
  const auto dataEnd = is.tellg();
  is.seekg(dataStart);
  if (dataEnd == std::istream::pos_type(-1) || dataEnd < dataStart ||
      !isStringLengthValid(len, static_cast<size_t>(dataEnd - dataStart), maxBytes) ||
      !hasStringAllocationHeadroom(len)) {
    s.clear();
    is.setstate(std::ios::failbit);
    return false;
  }

  s.resize(len);
  return len == 0 || static_cast<bool>(is.read(s.data(), len));
}

inline bool readString(HalFile& file, std::string& s, const size_t maxBytes = DEFAULT_MAX_STRING_BYTES) {
  uint32_t len = 0;
  if (file.read(&len, sizeof(len)) != sizeof(len)) {
    s.clear();
    return false;
  }

  const size_t position = file.position();
  const size_t fileSize = file.size();
  const size_t remaining = position <= fileSize ? fileSize - position : 0;
  if (!isStringLengthValid(len, remaining, maxBytes) || !hasStringAllocationHeadroom(len)) {
    s.clear();
    return false;
  }

  s.resize(len);
  if (len == 0) return true;
  if (file.read(s.data(), len) != static_cast<int>(len)) {
    s.clear();
    return false;
  }
  return true;
}
}  // namespace serialization
