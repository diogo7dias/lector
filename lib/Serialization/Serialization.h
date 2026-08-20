#pragma once
#include <HalStorage.h>

#include <iostream>

// The stream overloads, the length cap, and the reasoning behind both.
#include "SerializationLimits.h"

namespace serialization {
template <typename T>
void writePod(std::ostream& os, const T& value) {
  os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
void writePod(HalFile& file, const T& value) {
  file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

// Returns false when the card could not supply a whole T. The value is zeroed on failure
// rather than left holding whatever the caller's stack had: a short read used to produce
// an uninitialised length, which readString then tried to allocate.
template <typename T>
bool readPod(HalFile& file, T& value) {
  if (file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T)) != static_cast<int>(sizeof(T))) {
    memset(&value, 0, sizeof(T));
    return false;
  }
  return true;
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

// Mirrors readString(std::istream&) in SerializationLimits.h, which is where the host
// tests live. Refuses an implausible length before allocating; see that header for why an
// unbounded resize() here was a device crash rather than a recoverable error.
inline bool readString(HalFile& file, std::string& s) {
  s.clear();

  uint32_t len = 0;
  if (!readPod(file, len)) return false;

  const size_t here = file.position();
  const size_t total = file.size();
  if (total < here) return false;
  if (!stringLengthIsPlausible(len, static_cast<uint64_t>(total - here))) return false;
  if (len == 0) return true;

  s.resize(len);
  if (file.read(&s[0], len) != static_cast<int>(len)) {
    s.clear();
    return false;
  }
  return true;
}
}  // namespace serialization
