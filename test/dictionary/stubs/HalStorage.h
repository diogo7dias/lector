#pragma once

// Host-test stub for lib/hal/HalStorage.h: the subset Dictionary uses, on stdio, with card
// paths resolved under testCardRoot.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

inline std::string testCardRoot;

inline constexpr int O_WRITE = 1;
inline constexpr int O_CREAT = 2;
inline constexpr int O_TRUNC = 4;

class HalFile {
 public:
  HalFile() = default;
  ~HalFile() { close(); }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  HalFile(HalFile&& other) noexcept : fp_(other.fp_) { other.fp_ = nullptr; }
  HalFile& operator=(HalFile&& other) noexcept {
    close();
    fp_ = other.fp_;
    other.fp_ = nullptr;
    return *this;
  }

  bool open(const std::string& path, const char* mode) {
    close();
    fp_ = std::fopen(path.c_str(), mode);
    return fp_ != nullptr;
  }
  int read() {
    uint8_t byte;
    return read(&byte, 1) == 1 ? byte : -1;
  }
  int read(void* buffer, size_t count) { return fp_ ? static_cast<int>(std::fread(buffer, 1, count, fp_)) : -1; }
  int write(const void* buffer, size_t count) {
    return fp_ ? static_cast<int>(std::fwrite(buffer, 1, count, fp_)) : -1;
  }
  bool seekSet(size_t position) { return fp_ && std::fseek(fp_, static_cast<long>(position), SEEK_SET) == 0; }
  size_t position() const { return fp_ ? static_cast<size_t>(std::ftell(fp_)) : 0; }
  size_t fileSize() {
    if (!fp_) return 0;
    const long here = std::ftell(fp_);
    std::fseek(fp_, 0, SEEK_END);
    const long end = std::ftell(fp_);
    std::fseek(fp_, here, SEEK_SET);
    return static_cast<size_t>(end);
  }
  bool close() {
    if (!fp_) return false;
    const bool ok = std::fclose(fp_) == 0;
    fp_ = nullptr;
    return ok;
  }
  explicit operator bool() const { return fp_ != nullptr; }

 private:
  std::FILE* fp_ = nullptr;
};

class HalStorageStub {
 public:
  bool exists(const char* path) { return std::filesystem::exists(testCardRoot + path); }
  bool remove(const char* path) { return std::filesystem::remove(testCardRoot + path); }
  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    return file.open(testCardRoot + path, "rb");
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    return file.open(testCardRoot + path, "w+b");
  }
  HalFile open(const char* path, int) {
    HalFile file;
    file.open(testCardRoot + path, "w+b");
    return file;
  }
};
inline HalStorageStub Storage;
