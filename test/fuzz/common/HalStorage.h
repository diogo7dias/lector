#pragma once
// In-memory HalStorage for fuzz harnesses: paths built from fuzz input never reach the real
// disk, and fork-mode workers share nothing. readChunk caps every read() so the parsers'
// buffer-boundary paths run on small inputs too.
#include <Print.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>

class HalFile : public Print {
 public:
  HalFile() = default;
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  HalFile(HalFile&&) = default;
  HalFile& operator=(HalFile&&) = default;

  static inline size_t readChunk = SIZE_MAX;
  // Reads left before every read() fails with -1 like SdFat on an I/O error; < 0 never fails.
  static inline int readsBeforeError = -1;

  void open(std::shared_ptr<std::string> d) {
    data = std::move(d);
    pos = 0;
  }
  bool close() {
    const bool was = isOpen();
    data.reset();
    pos = 0;
    return was;
  }
  bool isOpen() const { return data != nullptr; }
  operator bool() const { return isOpen(); }
  void flush() {}
  size_t size() { return data ? data->size() : 0; }
  size_t fileSize() { return size(); }
  size_t position() const { return pos; }
  int available() const { return data ? static_cast<int>(data->size() - pos) : 0; }
  bool seek(size_t p) {
    if (!data || p > data->size()) return false;
    pos = p;
    return true;
  }
  bool seekSet(size_t p) { return seek(p); }
  bool seekCur(int64_t off) { return seek(static_cast<size_t>(static_cast<int64_t>(pos) + off)); }
  int read(void* buf, size_t count) {
    if (!data || readsBeforeError == 0) return -1;
    if (readsBeforeError > 0) --readsBeforeError;
    const size_t n = std::min({count, data->size() - pos, readChunk});
    memcpy(buf, data->data() + pos, n);
    pos += n;
    return static_cast<int>(n);
  }
  int read() {
    uint8_t b;
    return read(&b, 1) == 1 ? b : -1;
  }
  size_t write(const uint8_t* buf, size_t count) override {
    if (!data) return 0;
    // ponytail: writes land in RAM; cap them so a hostile book cannot grow the harness without bound.
    if (data->size() + count > (1u << 22)) return 0;
    data->replace(pos, std::min(count, data->size() - pos), reinterpret_cast<const char*>(buf), count);
    pos += count;
    return count;
  }
  size_t write(const void* buf, size_t count) { return write(static_cast<const uint8_t*>(buf), count); }
  size_t write(uint8_t b) override { return write(&b, 1); }

 private:
  std::shared_ptr<std::string> data;
  size_t pos = 0;
};

class HalStorage {
 public:
  struct StorageLock {};
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }
  std::map<std::string, std::shared_ptr<std::string>> files;

  void reset() { files.clear(); }
  void put(const std::string& path, std::string bytes) {
    files[path] = std::make_shared<std::string>(std::move(bytes));
  }
  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    const auto it = files.find(path);
    if (it == files.end()) return false;
    file.open(it->second);
    return true;
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    auto& slot = files[path];
    slot = std::make_shared<std::string>();
    file.open(slot);
    return true;
  }
  bool exists(const char* path) const { return files.count(path) > 0; }
  bool remove(const char* path) { return files.erase(path) > 0; }
  bool remove(const std::string& path) { return remove(path.c_str()); }
  bool rename(const char* from, const char* to) {
    const auto it = files.find(from);
    if (it == files.end() || files.count(to)) return false;
    files[to] = it->second;
    files.erase(from);
    return true;
  }
  bool mkdir(const char*, bool = true) { return true; }
};

#define Storage HalStorage::getInstance()

inline uint32_t millis() { return 0; }
inline void delay(uint32_t) {}
