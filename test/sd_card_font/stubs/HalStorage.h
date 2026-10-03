#pragma once

// Host-test stub for lib/hal/HalStorage.h: the read-only subset SdCardFont
// uses, backed by stdio on the local filesystem.

#include <cstdint>
#include <cstdio>

class HalFile {
 public:
  HalFile() = default;
  ~HalFile() { close(); }

  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  bool open(const char* path, const char* mode) {
    close();
    fp_ = std::fopen(path, mode);
    return fp_ != nullptr;
  }

  int read(void* buf, size_t count) {
    if (!fp_) return -1;
    return static_cast<int>(std::fread(buf, 1, count, fp_));
  }

  bool seekSet(uint64_t pos) { return fp_ && std::fseek(fp_, static_cast<long>(pos), SEEK_SET) == 0; }

  bool close() {
    if (!fp_) return false;
    std::fclose(fp_);
    fp_ = nullptr;
    return true;
  }

  operator bool() const { return fp_ != nullptr; }

 private:
  std::FILE* fp_ = nullptr;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }

  bool openFileForRead(const char*, const char* path, HalFile& file) { return file.open(path, "rb"); }
};

#define Storage HalStorage::getInstance()
