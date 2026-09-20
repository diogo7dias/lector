#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// In-memory SD adapter. Track reads even after close to detect speculative decoding.
struct HalFile {
  std::vector<uint8_t> bytes;
  size_t cursor = 0;
  size_t bytesRead = 0;
  bool opened = true;
  explicit operator bool() const { return opened; }
  size_t position() const { return cursor; }
  size_t size() const { return bytes.size(); }
  void seek(size_t pos) { cursor = pos; }
  void close() { opened = false; }
  int read(void* out, size_t count) {
    count = std::min(count, bytes.size() - std::min(cursor, bytes.size()));
    std::memcpy(out, bytes.data() + cursor, count);
    cursor += count;
    bytesRead += count;
    return static_cast<int>(count);
  }
  size_t write(const void* data, size_t count) {
    bytes.resize(std::max(bytes.size(), cursor + count));
    std::memcpy(bytes.data() + cursor, data, count);
    cursor += count;
    return count;
  }
};
struct MemoryStorage {
  std::vector<uint8_t> bytes;
  bool openFileForRead(const char*, const std::string&, HalFile& file) {
    file = HalFile{};
    file.bytes = bytes;
    return true;
  }
};
inline MemoryStorage Storage;
