#pragma once

#include <HalStorage.h>
#include <Print.h>

#include <algorithm>
#include <cstring>

// A serialization sink that compares against the existing file without retaining
// the JSON in RAM. A mismatch or read failure never suppresses a write.
class SerializedValueComparator : public Print {
  HalFile& file;
  uint8_t buffer[64];
  size_t offset = 0;
  size_t available = 0;
  bool equal = true;

 public:
  explicit SerializedValueComparator(HalFile& file) : file(file) {}

  size_t write(uint8_t byte) override { return write(&byte, 1); }

  size_t write(const uint8_t* bytes, size_t size) override {
    if (!equal) return 0;
    const size_t requested = size;
    while (size) {
      if (offset == available) {
        const int read = file.read(buffer, sizeof(buffer));
        if (read <= 0) {
          equal = false;
          return 0;
        }
        available = static_cast<size_t>(read);
        offset = 0;
      }
      const size_t count = std::min(size, available - offset);
      if (std::memcmp(bytes, buffer + offset, count) != 0) {
        equal = false;
        return 0;
      }
      offset += count;
      bytes += count;
      size -= count;
    }
    return requested;
  }

  bool matches() const { return equal; }
};
