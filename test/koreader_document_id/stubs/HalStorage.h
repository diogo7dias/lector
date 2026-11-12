#pragma once

#include <cstddef>
#include <cstring>
#include <string>

class HalFile {
 public:
  static inline size_t fileLength = 0;
  static inline bool seekFails = false;
  static inline int readResult = -2;  // -2: serve the whole request

  size_t fileSize() const { return fileLength; }
  bool seekSet(size_t) const { return !seekFails; }
  int read(void* dst, size_t len) const {
    std::memset(dst, 'x', len);
    return readResult == -2 ? static_cast<int>(len) : readResult;
  }
};

class HalStorage {
 public:
  bool openFileForRead(const char*, const std::string&, HalFile&) { return true; }
};

inline HalStorage Storage;
